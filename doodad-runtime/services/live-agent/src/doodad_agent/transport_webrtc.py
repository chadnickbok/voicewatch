"""Multi-device WebRTC signaling and bidirectional PCM adapter.

The CoreS3 has one shared codec path. It captures while the user is speaking and
plays while the assistant is speaking, so touch capture is the supported
interruption strategy during playback; this module does not claim simultaneous
acoustic barge-in on that hardware.
"""

from __future__ import annotations

import asyncio
import json
import os
import re
from collections.abc import Awaitable, Callable
from fractions import Fraction
from typing import TYPE_CHECKING, Any

import numpy as np
from aiohttp import WSMsgType, web
from aiortc import AudioStreamTrack, RTCPeerConnection, RTCSessionDescription
from av import AudioFrame
from av.audio.resampler import AudioResampler

from .metrics import LatencyTrace
from .audio import PcmSpool
from .session import ControlSession, WatchActionError
from .host_network import local_ipv4, keep_host_candidate

if TYPE_CHECKING:
    from .app_delivery import AppArtifactServer


AudioCallback = Callable[[str, bytes], Awaitable[None]]
EventCallback = Callable[[str, str, dict[str, Any]], Awaitable[None]]
IdentityCallback = Callable[["WatchSession", str, dict[str, Any]], Awaitable[None]]
DEVICE_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._:-]{7,63}$")


class DownlinkAudioTrack(PcmSpool, AudioStreamTrack):
    """Legacy aiortc adapter; RTP alone pads its final 20 ms packet."""

    def __init__(self, trace: LatencyTrace, *, max_spool_seconds: int = 600) -> None:
        AudioStreamTrack.__init__(self)
        PcmSpool.__init__(self, trace, max_spool_seconds=max_spool_seconds, pad_final_frame=True)

    async def recv(self) -> AudioFrame:
        packet = await self.recv_pcm()
        frame = AudioFrame(format="s16", layout="mono", samples=self._FRAME_SAMPLES)
        frame.planes[0].update(packet.data)
        frame.sample_rate = self._SAMPLE_RATE
        frame.pts = packet.pts
        frame.time_base = Fraction(1, self._SAMPLE_RATE)
        return frame

    async def wait_drained(self, timeout: float | None = None) -> None:
        deadline = (
            asyncio.get_running_loop().time() + timeout
            if timeout is not None
            else None
        )
        while not self._frames.empty() or self._frame_in_progress or self._pending_pcm:
            if deadline is not None and asyncio.get_running_loop().time() >= deadline:
                self._trace.mark("downlink.drain_timeout", pending_ms=self.pending_ms)
                raise TimeoutError(
                    f"downlink did not drain within {timeout:.3f}s "
                    f"({self.pending_ms}ms remains)"
                )
            await asyncio.sleep(0.02)
        # `recv()` removing the final sample means aiortc has accepted it, not
        # that the packet has cleared its encoder, socket, the ESP32 jitter
        # cache, and the CoreS3 speaker. Leave one bounded playout tail before
        # asking the shared codec to return to microphone capture.
        await asyncio.sleep(0.35)


class WatchSession(ControlSession):
    """One reconnectable, identity-bound watch signaling and media session."""

    def __init__(
        self,
        websocket: web.WebSocketResponse,
        trace: LatencyTrace,
        on_audio: AudioCallback,
        on_event: EventCallback,
        on_identified: IdentityCallback,
    ) -> None:
        super().__init__(websocket, trace, on_audio, on_event, on_identified)
        self.peer = RTCPeerConnection()
        self.downlink = DownlinkAudioTrack(
            trace,
            max_spool_seconds=int(
                os.getenv("DOODAD_DOWNLINK_MAX_SPOOL_SECONDS", "600")
            ),
        )
        self.sdp_chunks: list[str | None] | None = None
        self._audio_task: asyncio.Task[None] | None = None
        self._stats_task: asyncio.Task[None] | None = None
        self._downlink_attached = False
        self._resampler = AudioResampler(format="s16", layout="mono", rate=16_000)
        self._level_frames = 0
        self._level_peak = 0

        @self.peer.on("track")
        async def on_track(track: Any) -> None:
            self.trace.mark(
                "webrtc.track", device_id=self.device_id, media_kind=track.kind
            )
            if track.kind == "audio":
                self._audio_task = asyncio.create_task(self._consume_audio(track))
                self._audio_task.add_done_callback(self._report_audio_failure)

        @self.peer.on("connectionstatechange")
        async def on_state() -> None:
            state = self.peer.connectionState
            self.trace.mark("webrtc.state", device_id=self.device_id, state=state)
            if state == "connected":
                if self.device_id is None:
                    await self.close(code=4003, message=b"identity required")
                    return
                self.connected.set()
                if self._stats_task is None:
                    self._stats_task = asyncio.create_task(self._trace_inbound_stats())
                await self.on_event(self.device_id, "connected", {})

    async def receive(self, message: dict[str, Any]) -> None:
        if message.get("v") != 1 or not isinstance(message.get("type"), str):
            return
        kind = message["type"]
        payload = message.get("payload") or {}
        if kind == "hello":
            if not isinstance(payload, dict):
                await self.close(code=4002, message=b"invalid hello")
                return
            device_id = payload.get("device_id") or message.get("device_id")
            board = payload.get("board") or payload.get("device")
            capabilities = payload.get("capabilities") or {}
            if (
                not isinstance(device_id, str)
                or DEVICE_ID_PATTERN.fullmatch(device_id) is None
                or not isinstance(board, str)
                or not 1 <= len(board) <= 32
                or not isinstance(capabilities, dict)
            ):
                self.trace.mark("signal.identity_rejected")
                await self.close(code=4002, message=b"invalid device identity")
                return
            if self.device_id is not None and self.device_id != device_id:
                await self.close(code=4002, message=b"identity changed")
                return
            self.device_id = device_id
            self.board = board
            self.capabilities = dict(capabilities)
            await self.on_identified(self, device_id, payload)
            await self.send(
                "welcome",
                {
                    "mode": "live-agent",
                    "barge_in": "touch",
                    "audio": "opus-48000-rtp-16000-pcm-mono",
                },
            )
        elif self.device_id is None:
            self.trace.mark("signal.message_before_identity", message_type=kind)
            await self.close(code=4003, message=b"hello required first")
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
            assert self.device_id is not None
            await self.on_event(self.device_id, kind, payload)
        elif kind != "ice":
            if kind == "capture.started" and self.downlink.pending_ms:
                self.downlink.clear()
                self.trace.mark("conversation.interrupted", strategy="touch")
            assert self.device_id is not None
            await self.on_event(self.device_id, kind, payload)

    async def start_capture(self, duration_ms: int = 30_000) -> None:
        await self.send("capture.start", {"duration_ms": duration_ms})
        self.trace.mark("capture.command", state="started")

    async def stop_capture(self) -> None:
        await self.send("capture.stop", {})
        self.trace.mark("capture.command", state="stopped")

    def begin_downlink(self) -> None:
        self.downlink.begin_utterance()

    def enqueue_downlink(self, pcm: bytes, sample_rate: int) -> int:
        return self.downlink.enqueue_pcm(pcm, sample_rate)

    def end_downlink(self) -> int:
        return self.downlink.end_utterance()

    async def resume_after_downlink(self) -> None:
        await self.downlink.wait_drained()

    def clear_downlink(self) -> None:
        self.downlink.clear()

    async def close(self, *, code: int = 1000, message: bytes = b"") -> None:
        if self._closed:
            return
        self._closed = True
        self._fail_pending_actions()
        if self._audio_task is not None:
            self._audio_task.cancel()
            await asyncio.gather(self._audio_task, return_exceptions=True)
        if self._stats_task is not None:
            self._stats_task.cancel()
            await asyncio.gather(self._stats_task, return_exceptions=True)
        await self.peer.close()
        if not self.websocket.closed:
            await self.websocket.close(code=code, message=message)

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
                    assert self.device_id is not None
                    await self.on_audio(self.device_id, pcm)

    def _report_audio_failure(self, task: asyncio.Task[None]) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None:
            self.trace.mark(
                "uplink.consumer_failed",
                device_id=self.device_id,
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
                        device_id=self.device_id,
                        packets_received=getattr(report, "packetsReceived", 0),
                        packets_lost=getattr(report, "packetsLost", 0),
                        jitter=getattr(report, "jitter", 0),
                        transport_packets_received=transport_packets,
                        transport_bytes_received=transport_bytes,
                    )

    async def _accept_offer(self, sdp: str) -> None:
        if "opus/48000" not in sdp.lower():
            raise RuntimeError("watch SDP offer did not advertise Opus/48000")
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
        if "opus/48000" not in answer_sdp.lower():
            raise RuntimeError("host SDP answer did not negotiate Opus/48000")
        self.trace.mark(
            "webrtc.codec_negotiated",
            codec="opus",
            rtp_clock_rate=48_000,
            pcm_sample_rate=16_000,
        )
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
    """Own one endpoint and isolate sessions by stable device identity."""

    def __init__(
        self,
        trace: LatencyTrace,
        on_audio: AudioCallback,
        on_event: EventCallback,
        port: int = 8765,
        artifact_server: AppArtifactServer | None = None,
    ) -> None:
        self.trace = trace
        self.on_audio = on_audio
        self.on_event = on_event
        self.port = port
        self.artifact_server = artifact_server
        self.sessions: dict[str, WatchSession] = {}
        self._pending: set[WatchSession] = set()
        self._runner: web.AppRunner | None = None

    async def start(self) -> None:
        application = web.Application()
        application.router.add_get("/ws", self._websocket)
        if self.artifact_server is not None:
            self.artifact_server.add_routes(application)
        self._runner = web.AppRunner(application)
        await self._runner.setup()
        await web.TCPSite(self._runner, "0.0.0.0", self.port).start()

    async def close(self) -> None:
        sessions = list(self._pending | set(self.sessions.values()))
        self._pending.clear()
        self.sessions.clear()
        await asyncio.gather(
            *(session.close() for session in sessions), return_exceptions=True
        )
        if self._runner is not None:
            await self._runner.cleanup()
            self._runner = None

    async def _websocket(self, request: web.Request) -> web.WebSocketResponse:
        websocket = web.WebSocketResponse(max_msg_size=16 * 1024)
        await websocket.prepare(request)
        session = WatchSession(
            websocket, self.trace, self.on_audio, self.on_event, self._identify
        )
        self._pending.add(session)
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
            await self._finish_session(session)
        return websocket

    async def _finish_session(self, session: WatchSession) -> None:
        """Close one handler without tearing down a replacement session."""
        self._pending.discard(session)
        await session.close()
        device_id = session.device_id
        if device_id is None or self.sessions.get(device_id) is not session:
            return
        del self.sessions[device_id]
        await self.on_event(device_id, "disconnected", {})

    async def _identify(
        self, session: WatchSession, device_id: str, payload: dict[str, Any]
    ) -> None:
        self._pending.discard(session)
        prior = self.sessions.get(device_id)
        if prior is not None and prior is not session:
            await prior.close(code=4001, message=b"same device reconnected")
        self.sessions[device_id] = session
        self.trace.mark(
            "device.identified", device_id=device_id, board=session.board
        )
        await self.on_event(device_id, "identified", payload)
