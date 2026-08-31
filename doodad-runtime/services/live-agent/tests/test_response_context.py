"""Output authorization at the conversation boundary, without microphone frames."""
import asyncio
from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    InputAudioRawFrame, TranscriptionFrame, TTSSpeakFrame,
    VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame,
)

from doodad_agent.attention import AttentionBroker
from doodad_agent.capture_stt import frame_turn
from doodad_agent.capture_pipeline import provider_turn
from doodad_agent.conversation import LiveConversation
from doodad_agent.jobs import JobManager
from doodad_agent.metrics import LatencyTrace
from doodad_agent.moq_session import ResponseContextBusy
from doodad_agent.storage import Store


async def nothing(*args, **kwargs): pass


def conversation(authorize, attention=None):
    frames = []
    async def queue(frame): frames.append(frame)
    attention = attention or SimpleNamespace(background_snapshot=lambda: {})
    c = LiveConversation(None, None, attention, LatencyTrace(), lambda *_: 0,
                         nothing, nothing, nothing, nothing, nothing, nothing,
                         explicit_capture=True, authorize_response=authorize)
    c.worker = SimpleNamespace(queue_frame=queue)
    return c, frames


def no_microphone(frames):
    assert not any(isinstance(f, (InputAudioRawFrame, VADUserStartedSpeakingFrame,
                                 VADUserStoppedSpeakingFrame)) for f in frames)


@pytest.mark.asyncio
async def test_text_waits_for_watch_then_routes_without_microphone():
    entered, release = asyncio.Event(), asyncio.Event()
    async def authorize(kind):
        assert kind == 'text'
        entered.set()
        await release.wait()
    c, frames = conversation(authorize)
    task = asyncio.create_task(c.submit_text('Read my next exercise set.'))
    await asyncio.wait_for(entered.wait(), 1)
    assert not any(isinstance(f, TranscriptionFrame) for f in frames)
    release.set()
    await task
    transcriptions = [f for f in frames if isinstance(f, TranscriptionFrame)]
    assert len(transcriptions) == 1 and frame_turn(transcriptions[0]) is c._capture_turn
    assert c.voice_phase == 'thinking' and not c._capture_open
    await c.feed_audio(b'\0' * 320)
    no_microphone(frames)


@pytest.mark.asyncio
@pytest.mark.parametrize('replacement', ['cancel', 'capture', 'disconnect'])
async def test_late_authorization_cannot_submit_retired_text(replacement):
    entered, release = asyncio.Event(), asyncio.Event()
    async def authorize(kind): entered.set(); await release.wait()
    c, frames = conversation(authorize)
    task = asyncio.create_task(c.submit_text('Read my next exercise set.'))
    await asyncio.wait_for(entered.wait(), 1)
    original = c._capture_turn
    if replacement == 'cancel': await c.cancel()
    elif replacement == 'capture': await c.capture_started()
    else: c.disconnected()
    release.set()
    await task
    assert not original.live
    assert not any(isinstance(f, TranscriptionFrame) for f in frames)


@pytest.mark.asyncio
async def test_cancel_during_thinking_state_publish_cannot_submit_text():
    c, frames = conversation(nothing)
    async def state(phase, *args):
        if phase == 'thinking': await c.cancel()
    c.state_sink = state
    await c.submit_text('Read my next exercise set.')
    assert not any(isinstance(f, TranscriptionFrame) for f in frames)
    no_microphone(frames)


@pytest.mark.asyncio
async def test_background_busy_keeps_durable_announcement_pending(tmp_path):
    store = Store(tmp_path / 'attention.sqlite3')
    try:
        jobs = JobManager(store)
        broker = AttentionBroker(store, jobs)
        job = jobs.create('test_notification', {}, 1)
        event = jobs.append(job, 'completed', 'The test task is complete.', {}, 'test', 2)
        broker.observe(event, 2)
        async def busy(kind):
            assert kind == 'background'
            raise ResponseContextBusy('busy')
        c, frames = conversation(busy, broker)
        await c._deliver_idle_attention()
        assert broker.background_snapshot()['completion_pending'] == 1
        assert not any(isinstance(f, TTSSpeakFrame) for f in frames)
        c._authorize_response = nothing
        await c._deliver_idle_attention()
        speech = [f for f in frames if isinstance(f, TTSSpeakFrame)]
        assert len(speech) == 1 and frame_turn(speech[0]) is c._capture_turn
        assert broker.background_snapshot()['completion_pending'] == 1
        assert not c._capture_open
        no_microphone(frames)
    finally:
        store.close()


@pytest.mark.asyncio
@pytest.mark.parametrize('phase', ['listening', 'thinking', 'speaking'])
async def test_background_cannot_replace_foreground_turn(phase):
    calls = []
    async def authorize(kind): calls.append(kind)
    c, frames = conversation(authorize)
    await c.capture_started()
    original = c._capture_turn
    c.voice_phase = phase
    await c._deliver_idle_attention()
    assert not calls and c._capture_turn is original and original.live


@pytest.mark.asyncio
@pytest.mark.parametrize('outcome', ['played', 'cancelled', 'transport_cancelled'])
async def test_background_delivery_requires_matching_speaker_receipt(tmp_path, outcome):
    store = Store(tmp_path / 'attention.sqlite3')
    try:
        jobs = JobManager(store)
        broker = AttentionBroker(store, jobs)
        job = jobs.create('test_notification', {}, 1)
        event = jobs.append(job, 'completed', 'The test task is complete.', {}, 'test', 2)
        broker.observe(event, 2)
        c, _ = conversation(nothing, broker)
        await c._deliver_idle_attention()
        entered, release = asyncio.Event(), asyncio.Event()
        async def playback():
            entered.set()
            await release.wait()
            return outcome != 'transport_cancelled'
        c.wait_for_playback = playback
        token = provider_turn.set(c._capture_turn)
        task = asyncio.create_task(c._drain_downlink())
        provider_turn.reset(token)
        await asyncio.wait_for(entered.wait(), 1)
        assert broker.background_snapshot()['completion_pending'] == 1
        if outcome == 'cancelled': await c.cancel()
        release.set()
        played = await task
        assert played == (outcome == 'played')
        assert broker.background_snapshot()['completion_pending'] == (0 if played else 1)
    finally:
        store.close()


def test_deferred_question_is_not_focused_until_acknowledged(tmp_path):
    store = Store(tmp_path / 'question.sqlite3')
    try:
        jobs = JobManager(store)
        broker = AttentionBroker(store, jobs)
        job = jobs.create('test_notification', {}, 1)
        event = jobs.append(job, 'needs_input', 'Choose a layout.',
                            {'question': {'id': 'layout', 'prompt': 'Choose a layout.',
                                          'answer_schema': {'type': 'string'}}}, 'test', 2)
        broker.observe(event, 2)
        action = broker.natural_pause(3, defer_delivery=True)
        assert action.kind == 'question' and jobs.focused() is None
        other = AttentionBroker(store, JobManager(store, 'another-watch'))
        assert not other.acknowledge(action, 4) and jobs.focused() is None
        assert broker.natural_pause(4, defer_delivery=True) == action
        assert broker.acknowledge(action, 5)
        assert jobs.focused()['question_id'] == 'layout'
        assert not broker.acknowledge(action, 6)
    finally:
        store.close()


def test_unplayed_announcement_survives_broker_restart(tmp_path):
    path = tmp_path / 'restart.sqlite3'
    store = Store(path)
    jobs = JobManager(store)
    broker = AttentionBroker(store, jobs)
    job = jobs.create('test_notification', {}, 1)
    event = jobs.append(job, 'completed', 'The test task is complete.', {}, 'test', 2)
    broker.observe(event, 2)
    original = broker.natural_pause(3, defer_delivery=True)
    store.close()
    store = Store(path)
    try:
        broker = AttentionBroker(store, JobManager(store))
        assert broker.natural_pause(4, defer_delivery=True) == original
        assert broker.acknowledge(original, 5)
        assert not broker.acknowledge(original, 6)
        assert broker.natural_pause(6, defer_delivery=True) is None
    finally:
        store.close()
