"""Contiguous MoQ timestamps must not lose prebuffer to scheduler drift."""
import asyncio

import pytest

from doodad_agent.audio import AudioInterrupted, PacingOverrun, PcmSpool, _PacketPacer
from doodad_agent.metrics import LatencyTrace


class Clock:
    def __init__(self): self.now = 0.0
    async def sleep(self, seconds): self.now += seconds


@pytest.mark.asyncio
async def test_ten_minute_timeline_recovers_repeated_slips_without_clock_drift_or_bursts():
    clock = Clock()
    pacer = _PacketPacer(clock=lambda: clock.now, sleep=clock.sleep, continuous_timeline=True)
    previous = None
    for index in range(30000):
        if index and index % 250 == 0:
            clock.now += .035  # 15 ms past the next deadline, as observed on r7.
        assert await pacer.wait() == index * 320
        assert not pacer.last_reanchored
        if previous is not None:
            assert clock.now - previous >= .01 - 1e-8
        assert index * .02 - 1e-7 <= clock.now <= index * .02 + .015 + 1e-7
        previous = clock.now
    assert clock.now == pytest.approx(599.98, abs=1e-6)


@pytest.mark.asyncio
async def test_continuous_pacer_cannot_replay_a_backlog_outside_live_budget():
    clock = Clock()
    pacer = _PacketPacer(clock=lambda: clock.now, sleep=clock.sleep, continuous_timeline=True)
    assert await pacer.wait() == 0
    clock.now = .221  # The next packet was due at .020; >200 ms late.
    with pytest.raises(PacingOverrun):
        await pacer.wait()
    assert pacer._next_pts == 320


@pytest.mark.asyncio
async def test_retired_pacer_cannot_reanchor_replacement_response():
    entered, release = asyncio.Event(), asyncio.Event()
    clock = Clock()
    async def held(seconds):
        entered.set()
        await release.wait()
        clock.now += seconds
    spool = PcmSpool(LatencyTrace(), continuous_timeline=True)
    spool.begin_utterance()
    old_generation = spool.generation
    old = _PacketPacer(clock=lambda: clock.now, sleep=held, continuous_timeline=True)
    spool._pacer = old
    spool.enqueue_pcm(b'\x01\x00' * 640, 16000)
    assert (await spool.read(old_generation)).pts == 0
    pending = asyncio.create_task(spool.read(old_generation))
    await asyncio.wait_for(entered.wait(), 1)
    spool.clear()
    spool.begin_utterance()
    fresh = spool._pacer
    assert fresh is not old
    spool.enqueue_pcm(b'\x02\x00' * 320, 16000)
    packet = await spool.read(spool.generation)
    assert packet.pts == 0 and packet.data == b'\x02\x00' * 320
    deadline = fresh._next_deadline
    release.set()
    with pytest.raises(AudioInterrupted):
        await asyncio.wait_for(pending, 1)
    assert fresh._next_deadline == deadline and fresh._next_pts == 320
