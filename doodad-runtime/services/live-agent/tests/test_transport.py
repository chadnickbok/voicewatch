from __future__ import annotations

import asyncio
from dataclasses import dataclass, field

import numpy as np
import pytest
import soxr

from doodad_agent.metrics import LatencyTrace
from doodad_agent.transport import (
    DownlinkAudioTrack,
    DownlinkUtteranceBinding,
    _PacketPacer,
)


@dataclass
class FakeClock:
    now: float = 0.0
    sleeps: list[float] = field(default_factory=list)
    emitted_at: list[float] = field(default_factory=list)

    def __call__(self) -> float:
        return self.now

    async def sleep(self, seconds: float) -> None:
        self.sleeps.append(seconds)
        self.now += seconds


@pytest.mark.asyncio
@pytest.mark.parametrize("idle_gap", [0.25, 5.0])
async def test_packet_pacer_reanchors_after_idle_without_catch_up_burst(
    idle_gap: float,
) -> None:
    clock = FakeClock()
    pacer = _PacketPacer(clock=clock, sleep=clock.sleep)

    first_pts = await pacer.wait()
    clock.emitted_at.append(clock.now)
    second_pts = await pacer.wait()
    clock.emitted_at.append(clock.now)
    clock.now += idle_gap
    resumed_pts = await pacer.wait()
    clock.emitted_at.append(clock.now)
    following_pts = await pacer.wait()
    clock.emitted_at.append(clock.now)

    assert [first_pts, second_pts] == [0, 320]
    assert resumed_pts == second_pts + round(idle_gap * 16_000)
    assert following_pts == resumed_pts + 320
    intervals = np.diff(clock.emitted_at)
    assert intervals[0] == pytest.approx(0.02)
    assert intervals[1] == pytest.approx(idle_gap)
    assert intervals[2] == pytest.approx(0.02)
    assert min(intervals) >= 0.01


@pytest.mark.asyncio
async def test_packet_pacer_continuous_frames_are_twenty_milliseconds_apart() -> None:
    clock = FakeClock()
    pacer = _PacketPacer(clock=clock, sleep=clock.sleep)

    points: list[tuple[int, float]] = []
    for _ in range(5):
        points.append((await pacer.wait(), clock.now))

    assert [point[0] for point in points] == [0, 320, 640, 960, 1_280]
    assert np.diff([point[1] for point in points]) == pytest.approx([0.02] * 4)


def test_downlink_queues_only_complete_frames_and_pads_only_at_end() -> None:
    track = DownlinkAudioTrack(LatencyTrace())
    source = np.arange(319, dtype=np.int16)
    try:
        track.begin_utterance()
        assert track.enqueue_pcm(source.tobytes(), 16_000) == 319
        assert track._frames.empty()

        assert track.end_utterance() == 0
        _, payload = track._frames.get_nowait()
        decoded = np.frombuffer(payload, dtype="<i2")
        np.testing.assert_array_equal(decoded[:319], source)
        assert decoded[319] == 0
        assert track._frames.empty()
    finally:
        track.stop()


@pytest.mark.asyncio
async def test_downlink_builds_audio_frame_directly() -> None:
    clock = FakeClock()
    track = DownlinkAudioTrack(LatencyTrace())
    track._pacer = _PacketPacer(clock=clock, sleep=clock.sleep)
    source = np.arange(320, dtype=np.int16)
    try:
        track.begin_utterance()
        assert track.enqueue_pcm(source.tobytes(), 16_000) == 320
        track.end_utterance()

        frame = await track.recv()
        assert frame.format.name == "s16"
        assert frame.layout.name == "mono"
        assert frame.sample_rate == 16_000
        assert frame.samples == 320
        assert frame.pts == 0
        assert frame.time_base.numerator == 1
        assert frame.time_base.denominator == 16_000
        np.testing.assert_array_equal(
            np.frombuffer(bytes(frame.planes[0])[:640], dtype="<i2"),
            source,
        )
    finally:
        track.stop()


def test_streaming_resampler_matches_whole_utterance_conversion() -> None:
    track = DownlinkAudioTrack(LatencyTrace())
    phase = np.arange(24_000, dtype=np.float64)
    source = np.rint(np.sin(2 * np.pi * 660 * phase / 24_000) * 12_000).astype(np.int16)
    chunk_sizes = [137, 521, 83, 1_007, 61, 2_113, 977, 4_001, 6_103]
    offset = 0
    accepted = 0
    try:
        track.begin_utterance()
        for chunk_size in chunk_sizes:
            chunk = source[offset : offset + chunk_size]
            offset += chunk.size
            accepted += track.enqueue_pcm(chunk.tobytes(), 24_000)
        if offset < source.size:
            accepted += track.enqueue_pcm(source[offset:].tobytes(), 24_000)
        accepted += track.end_utterance()

        streamed = np.concatenate(
            [
                np.frombuffer(track._frames.get_nowait()[1], dtype="<i2")
                for _ in range(track._frames.qsize())
            ]
        )
        expected = soxr.resample(source, 24_000, 16_000, quality="HQ").astype(np.int16)
        assert accepted == expected.size == streamed.size
        np.testing.assert_allclose(streamed, expected, atol=2)
    finally:
        track.stop()


def test_downlink_rejects_sample_rate_change_and_traces_it() -> None:
    trace = LatencyTrace()
    events: list[tuple[str, dict[str, int]]] = []
    trace.mark = lambda kind, **fields: events.append((kind, fields)) or {}  # type: ignore[method-assign]
    track = DownlinkAudioTrack(trace)
    try:
        track.begin_utterance()
        track.enqueue_pcm(b"\0\0" * 480, 24_000)
        with pytest.raises(ValueError, match="sample rate changed"):
            track.enqueue_pcm(b"\0\0" * 320, 16_000)
        assert events == [
            ("downlink.sample_rate_rejected", {"expected": 24_000, "actual": 16_000})
        ]
    finally:
        track.stop()


@pytest.mark.asyncio
async def test_clear_discards_in_flight_frame_and_preserves_monotonic_pts() -> None:
    entered = asyncio.Event()
    release = asyncio.Event()

    class BlockingPacer:
        def __init__(self) -> None:
            self.pts = 0
            self.calls = 0

        async def wait(self) -> int:
            self.calls += 1
            if self.calls == 1:
                entered.set()
                await release.wait()
            result = self.pts
            self.pts += 320
            return result

    track = DownlinkAudioTrack(LatencyTrace())
    pacer = BlockingPacer()
    track._pacer = pacer  # type: ignore[assignment]
    stale = np.full(320, 111, dtype=np.int16)
    fresh = np.full(320, 222, dtype=np.int16)
    try:
        track.begin_utterance()
        track.enqueue_pcm(stale.tobytes(), 16_000)
        track.end_utterance()
        pending_recv = asyncio.create_task(track.recv())
        await entered.wait()

        track.clear()
        track.begin_utterance()
        track.enqueue_pcm(fresh.tobytes(), 16_000)
        track.end_utterance()
        release.set()

        frame = await asyncio.wait_for(pending_recv, 1)
        assert frame.pts == 320
        np.testing.assert_array_equal(
            np.frombuffer(bytes(frame.planes[0])[:640], dtype="<i2"),
            fresh,
        )
        assert pacer.calls == 2
    finally:
        track.stop()


def test_downlink_spools_five_minute_response_without_dropping_tail() -> None:
    track = DownlinkAudioTrack(LatencyTrace())
    source = np.arange(5 * 60 * 16_000, dtype=np.int16)
    try:
        track.begin_utterance()
        assert track.enqueue_pcm(source.tobytes(), 16_000) == source.size
        assert track.spooled_samples == source.size
        assert track.pending_ms == 5 * 60 * 1_000
        assert track._frames.qsize() == 5 * 60 * 50
        track.end_utterance()
    finally:
        track.stop()


def test_downlink_capacity_failure_is_explicit_and_never_partially_accepts() -> None:
    track = DownlinkAudioTrack(LatencyTrace(), max_spool_seconds=1)
    source = np.arange(16_001, dtype=np.int16)
    try:
        track.begin_utterance()
        with pytest.raises(BufferError, match="spool capacity"):
            track.enqueue_pcm(source.tobytes(), 16_000)
        assert track.spooled_samples == 0
        assert track.pending_ms == 0
        assert track._frames.empty()
    finally:
        track.stop()


def test_end_utterance_is_safe_after_interruption_clear() -> None:
    track = DownlinkAudioTrack(LatencyTrace())
    try:
        track.begin_utterance()
        track.enqueue_pcm(b"\0\0" * 480, 16_000)
        track.clear()
        assert track.end_utterance() == 0
        assert track.pending_ms == 0
        assert track.interrupted_samples > 0
    finally:
        track.stop()


def test_utterance_binding_drops_late_audio_after_touch_interruption() -> None:
    class Session:
        def __init__(self) -> None:
            self.active = False
            self.enqueued = 0
            self.cleared = 0

        def begin_downlink(self) -> None:
            self.active = True

        def enqueue_downlink(self, pcm: bytes, sample_rate: int) -> int:
            assert self.active
            assert sample_rate == 16_000
            accepted = len(pcm) // 2
            self.enqueued += accepted
            return accepted

        def end_downlink(self) -> int:
            self.active = False
            return 0

        def clear_downlink(self) -> None:
            self.active = False
            self.cleared += 1

    session = Session()
    binding = DownlinkUtteranceBinding()

    binding.begin(session)  # type: ignore[arg-type]
    assert binding.enqueue(session, b"\0\0" * 320, 16_000) == 320  # type: ignore[arg-type]
    binding.cancel()

    assert binding.enqueue(session, b"\0\0" * 320, 16_000) == 0  # type: ignore[arg-type]
    assert binding.end(session) == 0  # type: ignore[arg-type]
    assert session.enqueued == 320
    assert session.cleared == 1


def test_utterance_binding_drops_audio_after_session_replacement() -> None:
    class Session:
        def __init__(self) -> None:
            self.active = False
            self.cleared = 0

        def begin_downlink(self) -> None:
            self.active = True

        def enqueue_downlink(self, _pcm: bytes, _sample_rate: int) -> int:
            assert self.active
            return 320

        def end_downlink(self) -> int:
            self.active = False
            return 0

        def clear_downlink(self) -> None:
            self.active = False
            self.cleared += 1

    original = Session()
    replacement = Session()
    binding = DownlinkUtteranceBinding()

    binding.begin(original)  # type: ignore[arg-type]

    assert binding.enqueue(replacement, b"\0\0" * 320, 16_000) == 0  # type: ignore[arg-type]
    assert binding.end(replacement) == 0  # type: ignore[arg-type]
    assert original.cleared == 1
    assert replacement.cleared == 0


def test_finalized_utterance_remains_interruptible_until_playout_release() -> None:
    class Session:
        def __init__(self) -> None:
            self.active = False
            self.cleared = 0

        def begin_downlink(self) -> None:
            self.active = True

        def enqueue_downlink(self, pcm: bytes, _sample_rate: int) -> int:
            assert self.active
            return len(pcm) // 2

        def end_downlink(self) -> int:
            self.active = False
            return 0

        def clear_downlink(self) -> None:
            self.active = False
            self.cleared += 1

    session = Session()
    binding = DownlinkUtteranceBinding()

    binding.begin(session)  # type: ignore[arg-type]
    binding.enqueue(session, b"\0\0" * 320, 16_000)  # type: ignore[arg-type]
    binding.end(session)  # type: ignore[arg-type]
    binding.cancel()

    assert session.cleared == 1

    binding.begin(session)  # type: ignore[arg-type]
    binding.end(session)  # type: ignore[arg-type]
    binding.release(session)  # type: ignore[arg-type]
    binding.cancel()

    assert session.cleared == 1


@pytest.mark.asyncio
async def test_interruption_releases_an_unbounded_playout_wait() -> None:
    track = DownlinkAudioTrack(LatencyTrace())
    try:
        track.begin_utterance()
        track.enqueue_pcm(b"\0\0" * 320, 16_000)
        track.end_utterance()

        waiting = asyncio.create_task(track.wait_drained())
        await asyncio.sleep(0)
        assert not waiting.done()

        track.clear()
        await asyncio.wait_for(waiting, 1)
    finally:
        track.stop()


@pytest.mark.asyncio
async def test_stale_websocket_teardown_does_not_disconnect_replacement() -> None:
    events: list[tuple[str, str]] = []

    async def on_audio(_device_id: str, _pcm: bytes) -> None:
        return None

    async def on_event(
        device_id: str, kind: str, _payload: dict[str, object]
    ) -> None:
        events.append((device_id, kind))

    class Session:
        def __init__(self, device_id: str) -> None:
            self.device_id = device_id
            self.board = "cores3-se"
            self.closed = False

        async def close(self, **_kwargs: object) -> None:
            self.closed = True

    from doodad_agent.transport import WatchTransportServer

    server = WatchTransportServer(
        LatencyTrace(), on_audio, on_event, port=0  # type: ignore[arg-type]
    )
    original = Session("cores3-se-a")
    replacement = Session("cores3-se-a")
    server.sessions[replacement.device_id] = replacement  # type: ignore[assignment]

    await server._finish_session(original)  # type: ignore[arg-type]

    assert original.closed
    assert not replacement.closed
    assert server.sessions[replacement.device_id] is replacement
    assert events == []

    await server._finish_session(replacement)  # type: ignore[arg-type]

    assert replacement.closed
    assert replacement.device_id not in server.sessions
    assert events == [(replacement.device_id, "disconnected")]


@pytest.mark.asyncio
async def test_two_identities_coexist_and_reconnect_replaces_only_matching_device() -> None:
    events: list[tuple[str, str]] = []

    async def on_audio(_device_id: str, _pcm: bytes) -> None:
        return None

    async def on_event(
        device_id: str, kind: str, _payload: dict[str, object]
    ) -> None:
        events.append((device_id, kind))

    class Session:
        def __init__(self, device_id: str, board: str) -> None:
            self.device_id = device_id
            self.board = board
            self.closed = False

        async def close(self, **_kwargs: object) -> None:
            self.closed = True

    from doodad_agent.transport import WatchTransportServer

    server = WatchTransportServer(
        LatencyTrace(), on_audio, on_event, port=0
    )
    core = Session("cores3-se-a", "cores3-se")
    watch = Session("t-watch-s3-b", "t-watch-s3")
    await server._identify(core, core.device_id, {})  # type: ignore[arg-type]
    await server._identify(watch, watch.device_id, {})  # type: ignore[arg-type]

    assert set(server.sessions) == {core.device_id, watch.device_id}
    assert not core.closed and not watch.closed

    replacement = Session(core.device_id, "cores3-se")
    await server._identify(  # type: ignore[arg-type]
        replacement, replacement.device_id, {}
    )

    assert core.closed
    assert not watch.closed
    assert server.sessions[core.device_id] is replacement
    assert server.sessions[watch.device_id] is watch
