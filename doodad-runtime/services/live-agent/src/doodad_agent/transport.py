"""CoreS3 WebRTC signaling and bidirectional PCM adapter.

The CoreS3 has one shared codec path. It captures while the user is speaking and
plays while the assistant is speaking, so touch capture is the supported
interruption strategy during playback; this module does not claim simultaneous
acoustic barge-in on that hardware.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import re
import socket
import sys
from array import array
from collections.abc import Awaitable, Callable
from typing import Any

import numpy as np
import soxr
from aiohttp import WSMsgType, web
from aiortc import AudioStreamTrack, RTCPeerConnection, RTCSessionDescription
from av.audio.resampler import AudioResampler

from .metrics import LatencyTrace


AudioCallback = Callable[[bytes], Awaitable[None]]
EventCallback = Callable[[str, dict[str, Any]], Awaitable[None]]


class WatchActionError(RuntimeError):
    def __init__(self, code: str, message: str, revision: int | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.revision = revision


def local_ipv4() -> str:
    """Return the LAN address used in the host-only ICE answer."""
    if sys.platform == "darwin":
        import subprocess

        wifi = subprocess.run(
            ["ipconfig", "getifaddr", "en0"], check=False, capture_output=True, text=True
        ).stdout.strip()
        if wifi:
            return wifi
        route = subprocess.run(
            ["route", "-n", "get", "default"], check=True, capture_output=True, text=True
        ).stdout
        match = re.search(r"^\s*interface:\s*(\S+)", route, re.MULTILINE)
        if match:
            address = subprocess.run(
                ["ipconfig", "getifaddr", match.group(1)],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            if address:
                return address
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        return str(probe.getsockname()[0])
    finally:
        probe.close()


def keep_host_candidate(sdp: str, address: str) -> str:
    filtered: list[str] = []
    kept = False
    for line in sdp.splitlines():
        if not line.startswith("a=candidate:"):
            filtered.append(line)
        elif not kept and f" {address} " in line:
            filtered.append(line)
            kept = True
    return "\r\n".join(filtered) + "\r\n"


class DownlinkAudioTrack(AudioStreamTrack):
    """Paced 8 kHz mono source consumed by aiortc's PCMU sender."""

    def __init__(self, trace: LatencyTrace) -> None:
        super().__init__()
        self._samples: asyncio.Queue[int] = asyncio.Queue(maxsize=64_000)
        self._trace = trace
        self._first_audible = True
        self.audible_frames = 0

    def enqueue_pcm(self, pcm: bytes, sample_rate: int) -> int:
        source = np.frombuffer(pcm, dtype="<i2")
        if source.size == 0:
            return 0
        if sample_rate != 8_000:
            source = np.clip(
                soxr.resample(source.astype(np.float32), sample_rate, 8_000), -32768, 32767
            ).astype(np.int16)
        accepted = 0
        for sample in source:
            try:
                self._samples.put_nowait(int(sample))
                accepted += 1
            except asyncio.QueueFull:
                break
        return accepted

    def clear(self) -> None:
        while True:
            try:
                self._samples.get_nowait()
            except asyncio.QueueEmpty:
                break
        self._first_audible = True

    @property
    def pending_ms(self) -> int:
        return self._samples.qsize() // 8

    async def wait_drained(self, timeout: float = 20.0) -> None:
        deadline = asyncio.get_running_loop().time() + timeout
        while self._samples.qsize() and asyncio.get_running_loop().time() < deadline:
            await asyncio.sleep(0.02)
        # `recv()` removing the final sample means aiortc has accepted it, not
        # that the packet has cleared its encoder, socket, the ESP32 jitter
        # cache, and the CoreS3 speaker. Leave one bounded playout tail before
        # asking the shared codec to return to microphone capture.
        await asyncio.sleep(0.35)

    async def recv(self):  # type: ignore[no-untyped-def]
        # Do not transmit an endless silent RTP stream while the watch is
        # recording. The ESP32 peer's single manual loop otherwise spends its
        # budget consuming downlink packets and its uplink sender starves.
        first_sample = await self._samples.get()
        frame = await super().recv()
        samples = array("h", [first_sample])
        audible = first_sample != 0
        for _ in range(frame.samples - 1):
            try:
                sample = self._samples.get_nowait()
            except asyncio.QueueEmpty:
                sample = 0
            samples.append(sample)
            audible = audible or sample != 0
        frame.planes[0].update(samples.tobytes())
        if audible:
            self.audible_frames += 1
            if self._first_audible:
                self._trace.mark("downlink.first_audio")
                self._first_audible = False
        return frame


class WatchSession:
    """One reconnectable CoreS3 signaling and media session."""

    def __init__(
        self,
        websocket: web.WebSocketResponse,
        trace: LatencyTrace,
        on_audio: AudioCallback,
        on_event: EventCallback,
    ) -> None:
        self.websocket = websocket
        self.trace = trace
        self.on_audio = on_audio
        self.on_event = on_event
        self.peer = RTCPeerConnection()
        self.downlink = DownlinkAudioTrack(trace)
        self.sequence = 0
        self.connected = asyncio.Event()
        self.sdp_chunks: list[str | None] | None = None
        self._audio_task: asyncio.Task[None] | None = None
        self._stats_task: asyncio.Task[None] | None = None
        self._downlink_attached = False
        self._resampler = AudioResampler(format="s16", layout="mono", rate=16_000)
        self._pending_actions: dict[str, asyncio.Future[dict[str, Any]]] = {}
        self._level_frames = 0
        self._level_peak = 0

        @self.peer.on("track")
        async def on_track(track: Any) -> None:
            self.trace.mark("webrtc.track", media_kind=track.kind)
            if track.kind == "audio":
                self._audio_task = asyncio.create_task(self._consume_audio(track))
                self._audio_task.add_done_callback(self._report_audio_failure)

        @self.peer.on("connectionstatechange")
        async def on_state() -> None:
            state = self.peer.connectionState
            self.trace.mark("webrtc.state", state=state)
            if state == "connected":
                self.connected.set()
                if self._stats_task is None:
                    self._stats_task = asyncio.create_task(self._trace_inbound_stats())
                await self.on_event("connected", {})

    async def send(self, message_type: str, payload: dict[str, Any] | None = None) -> None:
        self.sequence += 1
        document = {"v": 1, "type": message_type, "seq": self.sequence}
        if payload is not None:
            document["payload"] = payload
        await self.websocket.send_str(json.dumps(document, separators=(",", ":")))

    async def receive(self, message: dict[str, Any]) -> None:
        if message.get("v") != 1 or not isinstance(message.get("type"), str):
            return
        kind = message["type"]
        payload = message.get("payload") or {}
        if kind == "hello":
            await self.send(
                "welcome",
                {"mode": "live-agent", "barge_in": "touch", "audio": "pcmu-8000-mono"},
            )
        elif kind == "sdp" and payload.get("kind") == "offer":
            if isinstance(payload.get("sdp"), str):
                await self._accept_offer(payload["sdp"])
        elif kind == "sdp.chunk":
            await self._receive_sdp_chunk(payload)
        elif kind == "action.result":
            request_id = payload.get("request_id")
            if isinstance(request_id, str):
                future = self._pending_actions.pop(request_id, None)
                if future is not None and not future.done():
                    future.set_result(payload)
            await self.on_event(kind, payload)
        elif kind != "ice":
            if kind == "capture.started" and self.downlink.pending_ms:
                self.downlink.clear()
                self.trace.mark("conversation.interrupted", strategy="touch")
            await self.on_event(kind, payload)

    async def start_capture(self, duration_ms: int = 30_000) -> None:
        await self.send("capture.start", {"duration_ms": duration_ms})
        self.trace.mark("capture.command", state="started")

    async def stop_capture(self) -> None:
        await self.send("capture.stop", {})
        self.trace.mark("capture.command", state="stopped")

    def enqueue_downlink(self, pcm: bytes, sample_rate: int) -> int:
        return self.downlink.enqueue_pcm(pcm, sample_rate)

    async def invoke_action(
        self,
        capability: str,
        arguments: dict[str, Any],
        idempotency_key: str,
        timeout: float = 8.0,
    ) -> dict[str, Any]:
        bounded_key = idempotency_key
        if len(bounded_key.encode("utf-8")) >= 65:
            bounded_key = "idem_" + hashlib.sha256(
                bounded_key.encode("utf-8")
            ).hexdigest()[:48]
        request_id = bounded_key
        if request_id in self._pending_actions:
            raise RuntimeError("action with this idempotency key is already pending")
        future: asyncio.Future[dict[str, Any]] = asyncio.get_running_loop().create_future()
        self._pending_actions[request_id] = future
        try:
            await self.send(
                "action.invoke",
                {
                    "request_id": request_id,
                    "capability": capability,
                    "idempotency_key": bounded_key,
                    "arguments": arguments,
                },
            )
            payload = await asyncio.wait_for(future, timeout)
        finally:
            self._pending_actions.pop(request_id, None)
        if not payload.get("ok"):
            error = payload.get("error") or {}
            raise WatchActionError(
                str(error.get("code", "action_failed")),
                str(error.get("message", "Watch action failed.")),
                int(error["revision"]) if "revision" in error else None,
            )
        result = dict(payload.get("result") or {})
        result["duplicate"] = bool(payload.get("duplicate"))
        return result

    async def resume_after_downlink(self) -> None:
        await self.downlink.wait_drained()
        await self.start_capture()

    async def close(self) -> None:
        for future in self._pending_actions.values():
            if not future.done():
                future.set_exception(ConnectionError("watch disconnected"))
        self._pending_actions.clear()
        if self._audio_task is not None:
            self._audio_task.cancel()
            await asyncio.gather(self._audio_task, return_exceptions=True)
        if self._stats_task is not None:
            self._stats_task.cancel()
            await asyncio.gather(self._stats_task, return_exceptions=True)
        await self.peer.close()

    async def _consume_audio(self, track: Any) -> None:
        first = True
        frames_received = 0
        while True:
            frame = await track.recv()
            frames_received += 1
            if frames_received % 100 == 0:
                self.trace.mark("uplink.frames", frames_received=frames_received)
            frame.pts = None
            for converted in self._resampler.resample(frame):
                pcm = bytes(converted.planes[0])[: converted.samples * 2]
                if pcm:
                    samples = np.frombuffer(pcm, dtype="<i2")
                    if samples.size:
                        self._level_peak = max(
                            self._level_peak,
                            int(np.max(np.abs(samples.astype(np.int32)))),
                        )
                    self._level_frames += 1
                    if self._level_frames == 100:
                        self.trace.mark("uplink.level", peak=self._level_peak)
                        self._level_frames = 0
                        self._level_peak = 0
                    if first:
                        self.trace.mark("uplink.first_audio")
                        first = False
                    await self.on_audio(pcm)

    def _report_audio_failure(self, task: asyncio.Task[None]) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None:
            self.trace.mark(
                "uplink.consumer_failed",
                error_type=type(error).__name__,
                message=str(error)[:160],
            )

    async def _trace_inbound_stats(self) -> None:
        while True:
            await asyncio.sleep(2)
            stats = await self.peer.getStats()
            transport_packets = 0
            transport_bytes = 0
            for report in stats.values():
                if report.type == "transport":
                    transport_packets += int(getattr(report, "packetsReceived", 0))
                    transport_bytes += int(getattr(report, "bytesReceived", 0))
            for report in stats.values():
                if report.type == "inbound-rtp" and getattr(report, "kind", None) == "audio":
                    self.trace.mark(
                        "webrtc.inbound_stats",
                        packets_received=getattr(report, "packetsReceived", 0),
                        packets_lost=getattr(report, "packetsLost", 0),
                        jitter=getattr(report, "jitter", 0),
                        transport_packets_received=transport_packets,
                        transport_bytes_received=transport_bytes,
                    )

    async def _accept_offer(self, sdp: str) -> None:
        await self.peer.setRemoteDescription(RTCSessionDescription(sdp=sdp, type="offer"))
        self.trace.mark(
            "webrtc.remote_description",
            transceivers=len(self.peer.getTransceivers()),
            has_sendrecv="a=sendrecv" in sdp,
        )
        if not self._downlink_attached:
            self.peer.addTrack(self.downlink)
            self._downlink_attached = True
        answer = await self.peer.createAnswer()
        await self.peer.setLocalDescription(answer)
        assert self.peer.localDescription is not None
        answer_sdp = keep_host_candidate(self.peer.localDescription.sdp, local_ipv4())
        await self.send("sdp", {"kind": "answer", "sdp": answer_sdp})

    async def _receive_sdp_chunk(self, payload: dict[str, Any]) -> None:
        index, count, data = payload.get("index"), payload.get("count"), payload.get("data")
        if (
            payload.get("kind") != "offer"
            or not isinstance(index, int)
            or not isinstance(count, int)
            or not isinstance(data, str)
            or not 1 <= count <= 256
            or not 0 <= index < count
        ):
            return
        if self.sdp_chunks is None or len(self.sdp_chunks) != count:
            self.sdp_chunks = [None] * count
        self.sdp_chunks[index] = data
        if all(chunk is not None for chunk in self.sdp_chunks):
            offer = "".join(chunk or "" for chunk in self.sdp_chunks)
            self.sdp_chunks = None
            await self._accept_offer(offer)


class WatchTransportServer:
    """Own the aiohttp endpoint and replace stale sessions on reconnect."""

    def __init__(
        self,
        trace: LatencyTrace,
        on_audio: AudioCallback,
        on_event: EventCallback,
        port: int = 8765,
    ) -> None:
        self.trace = trace
        self.on_audio = on_audio
        self.on_event = on_event
        self.port = port
        self.session: WatchSession | None = None
        self._runner: web.AppRunner | None = None

    async def start(self) -> None:
        application = web.Application()
        application.router.add_get("/ws", self._websocket)
        self._runner = web.AppRunner(application)
        await self._runner.setup()
        await web.TCPSite(self._runner, "0.0.0.0", self.port).start()

    async def close(self) -> None:
        if self.session is not None:
            await self.session.close()
            self.session = None
        if self._runner is not None:
            await self._runner.cleanup()
            self._runner = None

    async def _websocket(self, request: web.Request) -> web.WebSocketResponse:
        websocket = web.WebSocketResponse(max_msg_size=16 * 1024)
        await websocket.prepare(request)
        if self.session is not None:
            await self.session.close()
        session = WatchSession(websocket, self.trace, self.on_audio, self.on_event)
        self.session = session
        try:
            async for raw in websocket:
                if raw.type == WSMsgType.TEXT:
                    try:
                        await session.receive(json.loads(raw.data))
                    except (json.JSONDecodeError, TypeError, ValueError):
                        self.trace.mark("signal.invalid")
                elif raw.type == WSMsgType.ERROR:
                    break
        finally:
            await session.close()
            if self.session is session:
                self.session = None
        return websocket
