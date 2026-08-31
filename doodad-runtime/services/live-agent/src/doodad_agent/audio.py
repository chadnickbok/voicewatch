"""Transport-neutral PCM spooling, resampling and pacing; no RTP dependency."""
from __future__ import annotations

import asyncio
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

import numpy as np
import soxr

from .metrics import LatencyTrace


class AudioInterrupted(Exception):
    """The caller's audio generation was cancelled or replaced."""


@dataclass(frozen=True)
class PcmPacket:
    generation: int
    pts: int
    data: bytes


class _PacketPacer:
    """Maintain a monotonic sample clock without replaying time lost while idle."""

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
        """Wait for one packet deadline and return its monotonic sample PTS."""
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
            # the sample clock forward by wall time and start a fresh cadence rather
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


class PcmSpool:
    """Bounded PCM16k utterance spool; one event-loop producer and reader.

    The transport owns encoding and remote playback completion. Each read is
    bound to a generation, including across pacing awaits and cancellation.
    Final PCM is exact unless a legacy adapter explicitly requests padding.
    """

    _SAMPLE_RATE = 16_000
    _FRAME_SAMPLES = 320
    _DEFAULT_MAX_SPOOL_SECONDS = 600

    def __init__(
        self,
        trace: LatencyTrace,
        *,
        max_spool_seconds: int = _DEFAULT_MAX_SPOOL_SECONDS,
        pad_final_frame: bool = False,
    ) -> None:
        if max_spool_seconds <= 0:
            raise ValueError("max_spool_seconds must be positive")
        self._pad_final_frame = pad_final_frame
        self._changed = asyncio.Event()
        self._ended = False
        self._queued_samples = 0
        self._inflight_samples = 0
        self._inflight_generation: int | None = None
        self._max_spool_samples = max_spool_seconds * self._SAMPLE_RATE
        # This is a server-side utterance spool, not the watch jitter buffer.
        # TTS providers commonly return audio much faster than realtime, so a
        # short bounded asyncio queue silently truncates otherwise valid long
        # answers. Keep complete frames here and pace them only in read().
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
        if not self._frames.empty() or self._inflight_generation == self._generation:
            raise RuntimeError("previous downlink utterance has not drained")
        self._generation += 1
        self._ended = False
        self._utterance_active = True
        self._changed.set()
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
        try:
            return self._accept_output(output)
        except BufferError:
            # A streaming resampler has consumed source samples already. A
            # retry cannot safely continue that codec epoch after overflow.
            self.clear()
            raise

    def end_utterance(self) -> int:
        if not self._utterance_active:
            return 0
        accepted = 0
        try:
            if self._resampler is not None:
                accepted = self._accept_output(
                    self._resampler.resample_chunk(
                        np.empty(0, dtype=np.int16),
                        last=True,
                    )
                )
        except BufferError:
            self.clear()
            raise
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
        self._ended = True
        self._changed.set()
        return accepted

    def clear(self) -> None:
        self._generation += 1
        discarded = self._queued_samples + self._inflight_samples + len(self._pending_pcm) // 2
        while True:
            try:
                self._frames.get_nowait()
            except asyncio.QueueEmpty:
                break
        self._queued_samples = 0
        self._inflight_samples = 0
        self._pending_pcm.clear()
        self._ended = True
        self._changed.set()
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
        samples = self._queued_samples + self._inflight_samples + len(self._pending_pcm) // 2
        return samples // (self._SAMPLE_RATE // 1_000)

    @property
    def generation(self) -> int:
        return self._generation

    @property
    def utterance_samples(self) -> int:
        return self._utterance_spooled_samples

    async def read(self, generation: int) -> PcmPacket | None:
        """Read one paced packet, or None after end and local drain.

        None only describes this spool. It is never a remote playout receipt.
        A cancelled read cannot consume samples from a replacement utterance.
        """
        while True:
            if generation != self._generation:
                raise AudioInterrupted("downlink generation retired")
            if not self._frames.empty():
                break
            if self._ended:
                return None
            self._changed.clear()
            await self._changed.wait()
        frame_generation, payload = self._frames.get_nowait()
        self._queued_samples -= len(payload) // 2
        self._frame_in_progress = True
        self._inflight_generation = generation
        self._inflight_samples = len(payload) // 2
        try:
            pts = await self._pacer.wait()
            if generation != self._generation or frame_generation != generation:
                raise AudioInterrupted("downlink generation retired")
            if getattr(self._pacer, "last_reanchored", False):
                self._trace.mark(
                    "downlink.pacer_reanchored",
                    interval_ms=round(self._pacer.last_interval_ms, 3),
                    pts=pts,
                )
            if any(payload):
                self.audible_frames += 1
                if self._first_audible:
                    self._trace.mark("downlink.first_audio")
                    self._first_audible = False
            return PcmPacket(generation, pts, payload)
        finally:
            self._frame_in_progress = False
            self._inflight_generation = None
            self._inflight_samples = 0

    async def recv_pcm(self) -> PcmPacket:
        """Legacy continuous reader, waiting across utterance boundaries."""
        while True:
            try:
                packet = await self.read(self._generation)
                if packet is not None:
                    return packet
            except AudioInterrupted:
                continue
            self._changed.clear()
            await self._changed.wait()

    def _accept_output(self, output: np.ndarray) -> int:
        if output.size == 0:
            return 0
        queued_samples = self._queued_samples + self._inflight_samples
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
            self._queued_samples + self._inflight_samples + len(self._pending_pcm) // 2,
        )
        return requested

    def _queue_complete_frames(self) -> None:
        frame_bytes = self._FRAME_SAMPLES * 2
        while len(self._pending_pcm) >= frame_bytes:
            payload = bytes(self._pending_pcm[:frame_bytes])
            del self._pending_pcm[:frame_bytes]
            self._put_frame(payload)

    def _queue_final_frame(self) -> None:
        if not self._pending_pcm:
            return
        frame_bytes = self._FRAME_SAMPLES * 2
        payload = bytes(self._pending_pcm)
        if self._pad_final_frame:
            payload = payload.ljust(frame_bytes, b"\0")
        self._pending_pcm.clear()
        self._put_frame(payload)


    def _put_frame(self, payload: bytes) -> None:
        self._frames.put_nowait((self._generation, payload))
        self._queued_samples += len(payload) // 2
        self._changed.set()
