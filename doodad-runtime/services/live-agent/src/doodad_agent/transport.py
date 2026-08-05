"""Multi-device WebRTC signaling and bidirectional PCM adapter.

The CoreS3 has one shared codec path. It captures while the user is speaking and
plays while the assistant is speaking, so touch capture is the supported
interruption strategy during playback; this module does not claim simultaneous
acoustic barge-in on that hardware.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
import re
import socket
import sys
import time
from collections.abc import Awaitable, Callable
from fractions import Fraction
from typing import TYPE_CHECKING, Any

import numpy as np
import soxr
from aiohttp import WSMsgType, web
from aiortc import AudioStreamTrack, RTCPeerConnection, RTCSessionDescription
from av import AudioFrame
from av.audio.resampler import AudioResampler

from .metrics import LatencyTrace

if TYPE_CHECKING:
    from .app_delivery import AppArtifactServer


AudioCallback = Callable[[str, bytes], Awaitable[None]]
EventCallback = Callable[[str, str, dict[str, Any]], Awaitable[None]]
IdentityCallback = Callable[["WatchSession", str, dict[str, Any]], Awaitable[None]]
DEVICE_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._:-]{7,63}$")


class _PacketPacer:
    """Maintain a monotonic RTP clock without replaying time lost while idle."""

    def __init__(
        self,
        *,
        sample_rate: int = 16_000,
        frame_samples: int = 320,
        clock: Callable[[], float] | None = None,
        sleep: Callable[[float], Awaitable[None]] | None = None,
    ) -> None:
        self.sample_rate = sample_rate
        self.frame_samples = frame_samples
        self.frame_period = frame_samples / sample_rate
        self._clock = clock or time.monotonic
        self._sleep = sleep or asyncio.sleep
        self._last_emit_at: float | None = None
        self._last_pts: int | None = None
        self._next_deadline: float | None = None
        self._next_pts = 0
        self.last_reanchored = False
        self.last_interval_ms = 0.0

    async def wait(self) -> int:
        """Wait for one packet deadline and return its monotonic RTP PTS."""
        now = self._clock()
        if self._last_emit_at is None:
            pts = self._next_pts
            emitted_at = now
            reanchored = True
            self.last_reanchored = False
            self.last_interval_ms = 0.0
        else:
            assert self._last_pts is not None
            assert self._next_deadline is not None
            if now < self._next_deadline:
                await self._sleep(self._next_deadline - now)
                now = self._clock()

            # A scheduler slip under half a packet still leaves at least 10 ms
            # before the following deadline. Larger gaps are idle periods: move
            # the RTP clock forward by wall time and start a fresh cadence rather
            # than emitting a catch-up burst.
            reanchored = now - self._next_deadline >= self.frame_period / 2
            if reanchored:
                elapsed_samples = max(
                    self.frame_samples,
                    round((now - self._last_emit_at) * self.sample_rate),
                )
                pts = max(self._next_pts, self._last_pts + elapsed_samples)
            else:
                pts = self._next_pts
            emitted_at = now
            self.last_reanchored = reanchored
            self.last_interval_ms = (emitted_at - self._last_emit_at) * 1_000

        self._last_emit_at = emitted_at
        self._last_pts = pts
        self._next_pts = pts + self.frame_samples
        if reanchored or self._next_deadline is None:
            self._next_deadline = emitted_at + self.frame_period
        else:
            self._next_deadline += self.frame_period
        return pts


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
    """Paced 16 kHz mono source consumed by aiortc's Opus sender."""

    _SAMPLE_RATE = 16_000
    _FRAME_SAMPLES = 320
    _DEFAULT_MAX_SPOOL_SECONDS = 600

    def __init__(
        self,
        trace: LatencyTrace,
        *,
        max_spool_seconds: int = _DEFAULT_MAX_SPOOL_SECONDS,
    ) -> None:
        super().__init__()
        if max_spool_seconds <= 0:
            raise ValueError("max_spool_seconds must be positive")
        self._max_spool_samples = max_spool_seconds * self._SAMPLE_RATE
        # This is a server-side utterance spool, not the watch jitter buffer.
        # TTS providers commonly return audio much faster than realtime, so a
        # short bounded asyncio queue silently truncates otherwise valid long
        # answers. Keep complete frames here and pace them only in recv().
        self._frames: asyncio.Queue[tuple[int, bytes]] = asyncio.Queue()
        self._trace = trace
        self._pacer = _PacketPacer(
            sample_rate=self._SAMPLE_RATE,
            frame_samples=self._FRAME_SAMPLES,
        )
        self._generation = 0
        self._pending_pcm = bytearray()
        self._input_sample_rate: int | None = None
        self._resampler: soxr.ResampleStream | None = None
        self._utterance_active = False
        self._frame_in_progress = False
        self._first_audible = True
        self.audible_frames = 0
        self.spooled_samples = 0
        self.interrupted_samples = 0
        self._utterance_spooled_samples = 0
        self._spool_high_water_samples = 0

    def begin_utterance(self) -> None:
        if self._utterance_active:
            raise RuntimeError("downlink utterance is already active")
        if self._pending_pcm:
            raise RuntimeError("previous downlink utterance was not finalized")
        self._utterance_active = True
        self._input_sample_rate = None
        self._resampler = None
        self._first_audible = True
        self._utterance_spooled_samples = 0
        self._spool_high_water_samples = 0

    def enqueue_pcm(self, pcm: bytes, sample_rate: int) -> int:
        if not self._utterance_active:
            raise RuntimeError("begin_utterance() must be called before enqueue_pcm()")
        if sample_rate <= 0:
            raise ValueError("sample_rate must be positive")
        if len(pcm) % 2:
            raise ValueError("16-bit PCM must contain an even number of bytes")
        source = np.frombuffer(pcm, dtype="<i2")
        if source.size == 0:
            return 0

        if self._input_sample_rate is None:
            self._input_sample_rate = sample_rate
            if sample_rate != self._SAMPLE_RATE:
                self._resampler = soxr.ResampleStream(
                    sample_rate,
                    self._SAMPLE_RATE,
                    1,
                    dtype="int16",
                    quality="HQ",
                )
        elif sample_rate != self._input_sample_rate:
            self._trace.mark(
                "downlink.sample_rate_rejected",
                expected=self._input_sample_rate,
                actual=sample_rate,
            )
            raise ValueError(
                "downlink sample rate changed within an utterance "
                f"({self._input_sample_rate} -> {sample_rate})"
            )

        output = (
            source
            if self._resampler is None
            else self._resampler.resample_chunk(source, last=False)
        )
        return self._accept_output(output)

    def end_utterance(self) -> int:
        if not self._utterance_active:
            return 0
        accepted = 0
        if self._resampler is not None:
            accepted = self._accept_output(
                self._resampler.resample_chunk(
                    np.empty(0, dtype=np.int16),
                    last=True,
                )
            )
        self._queue_final_frame()
        self._trace.mark(
            "downlink.utterance_spooled",
            samples=self._utterance_spooled_samples,
            duration_ms=self._utterance_spooled_samples
            // (self._SAMPLE_RATE // 1_000),
            high_water_ms=self._spool_high_water_samples
            // (self._SAMPLE_RATE // 1_000),
        )
        self._utterance_active = False
        self._input_sample_rate = None
        self._resampler = None
        return accepted

    def clear(self) -> None:
        self._generation += 1
        discarded = self._frames.qsize() * self._FRAME_SAMPLES + len(self._pending_pcm) // 2
        while True:
            try:
                self._frames.get_nowait()
            except asyncio.QueueEmpty:
                break
        self._pending_pcm.clear()
        self._utterance_active = False
        self._input_sample_rate = None
        self._resampler = None
        self._first_audible = True
        if discarded:
            self.interrupted_samples += discarded
            self._trace.mark(
                "downlink.spool_cleared",
                discarded_samples=discarded,
                total_interrupted_samples=self.interrupted_samples,
            )

    @property
    def pending_ms(self) -> int:
        samples = self._frames.qsize() * self._FRAME_SAMPLES + len(self._pending_pcm) // 2
        return samples // (self._SAMPLE_RATE // 1_000)

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

    async def recv(self):  # type: ignore[no-untyped-def]
        # Do not transmit an endless silent RTP stream while the watch is
        # recording. The ESP32 peer's single manual loop otherwise spends its
        # budget consuming downlink packets and its uplink sender starves.
        while True:
            generation, payload = await self._frames.get()
            self._frame_in_progress = True
            try:
                pts = await self._pacer.wait()
                if generation != self._generation:
                    continue
                if getattr(self._pacer, "last_reanchored", False):
                    self._trace.mark(
                        "downlink.pacer_reanchored",
                        interval_ms=round(self._pacer.last_interval_ms, 3),  # type: ignore[attr-defined]
                        pts=pts,
                    )
                frame = AudioFrame(
                    format="s16",
                    layout="mono",
                    samples=self._FRAME_SAMPLES,
                )
                frame.planes[0].update(payload)
                frame.sample_rate = self._SAMPLE_RATE
                frame.pts = pts
                frame.time_base = Fraction(1, self._SAMPLE_RATE)
                if any(payload):
                    self.audible_frames += 1
                    if self._first_audible:
                        self._trace.mark("downlink.first_audio")
                        self._first_audible = False
                return frame
            finally:
                self._frame_in_progress = False

    def _accept_output(self, output: np.ndarray) -> int:
        if output.size == 0:
            return 0
        queued_samples = self._frames.qsize() * self._FRAME_SAMPLES
        pending_samples = len(self._pending_pcm) // 2
        requested = int(output.size)
        projected = queued_samples + pending_samples + requested
        if projected > self._max_spool_samples:
            self._trace.mark(
                "downlink.spool_capacity_exceeded",
                requested_samples=requested,
                queued_samples=queued_samples + pending_samples,
                capacity_samples=self._max_spool_samples,
            )
            raise BufferError(
                "downlink utterance exceeds configured server spool capacity "
                f"({projected}/{self._max_spool_samples} samples)"
            )
        encoded = output.astype("<i2", copy=False).tobytes()
        self._pending_pcm.extend(encoded)
        self._queue_complete_frames()
        self.spooled_samples += requested
        self._utterance_spooled_samples += requested
        self._spool_high_water_samples = max(
            self._spool_high_water_samples,
            self._frames.qsize() * self._FRAME_SAMPLES + len(self._pending_pcm) // 2,
        )
        return requested

    def _queue_complete_frames(self) -> None:
        frame_bytes = self._FRAME_SAMPLES * 2
        while len(self._pending_pcm) >= frame_bytes:
            payload = bytes(self._pending_pcm[:frame_bytes])
            del self._pending_pcm[:frame_bytes]
            self._frames.put_nowait((self._generation, payload))

    def _queue_final_frame(self) -> None:
        if not self._pending_pcm:
            return
        frame_bytes = self._FRAME_SAMPLES * 2
        payload = bytes(self._pending_pcm).ljust(frame_bytes, b"\0")
        self._pending_pcm.clear()
        self._frames.put_nowait((self._generation, payload))


class WatchSession:
    """One reconnectable, identity-bound watch signaling and media session."""

    def __init__(
        self,
        websocket: web.WebSocketResponse,
        trace: LatencyTrace,
        on_audio: AudioCallback,
        on_event: EventCallback,
        on_identified: IdentityCallback,
    ) -> None:
        self.websocket = websocket
        self.trace = trace
        self.on_audio = on_audio
        self.on_event = on_event
        self.on_identified = on_identified
        self.device_id: str | None = None
        self.board: str | None = None
        self.capabilities: dict[str, Any] = {}
        self.peer = RTCPeerConnection()
        self.downlink = DownlinkAudioTrack(
            trace,
            max_spool_seconds=int(
                os.getenv("DOODAD_DOWNLINK_MAX_SPOOL_SECONDS", "600")
            ),
        )
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
        self._closed = False

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

    async def send(self, message_type: str, payload: dict[str, Any] | None = None) -> None:
        self.sequence += 1
        document = {"v": 1, "type": message_type, "seq": self.sequence}
        if self.device_id is not None:
            document["device_id"] = self.device_id
        if payload is not None:
            document["payload"] = payload
        await self.websocket.send_str(json.dumps(document, separators=(",", ":")))

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

    def clear_downlink(self) -> None:
        self.downlink.clear()

    async def close(self, *, code: int = 1000, message: bytes = b"") -> None:
        if self._closed:
            return
        self._closed = True
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


class DownlinkUtteranceBinding:
    """Keep one TTS utterance attached to the session that started it.

    Pipecat may deliver already-buffered audio after an interruption, and the
    watch may reconnect between TTS lifecycle frames. Stale chunks are dropped
    instead of being enqueued into an inactive or replacement session.
    """

    def __init__(self) -> None:
        self._session: WatchSession | None = None
        self._finalized = False

    def begin(self, session: WatchSession | None) -> None:
        self.cancel()
        if session is None:
            return
        session.begin_downlink()
        self._session = session
        self._finalized = False

    def enqueue(
        self,
        current_session: WatchSession | None,
        pcm: bytes,
        sample_rate: int,
    ) -> int:
        if self._session is None:
            return 0
        if self._session is not current_session:
            self.cancel()
            return 0
        if self._finalized:
            return 0
        return self._session.enqueue_downlink(pcm, sample_rate)

    def end(self, current_session: WatchSession | None) -> int:
        session = self._session
        if session is None:
            return 0
        if self._finalized:
            return 0
        if session is not current_session:
            self.cancel()
            return 0
        accepted = session.end_downlink()
        self._finalized = True
        return accepted

    def release(self, current_session: WatchSession | None) -> None:
        """Detach a normally drained utterance without clearing its track."""

        session = self._session
        self._session = None
        self._finalized = False
        if session is not None and session is not current_session:
            session.clear_downlink()

    def cancel(self) -> None:
        session = self._session
        self._session = None
        self._finalized = False
        if session is not None:
            session.clear_downlink()


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
