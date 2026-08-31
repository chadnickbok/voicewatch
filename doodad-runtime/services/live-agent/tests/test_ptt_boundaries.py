from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    InputAudioRawFrame, InterruptionFrame, VADUserStartedSpeakingFrame,
    VADUserStoppedSpeakingFrame,
)
from pipecat.processors.frame_processor import FrameDirection, FrameProcessorQueue

from doodad_agent.conversation import LiveConversation
from doodad_agent import conversation as conversation_module
from doodad_agent.metrics import LatencyTrace


async def nothing(*_): pass


def conversation(frames):
    async def queue(frame): frames.append(frame)
    result = LiveConversation(None, None, None, LatencyTrace(), lambda *_: 0,
        nothing, nothing, nothing, nothing, nothing, nothing, explicit_capture=True)
    result.worker = SimpleNamespace(queue_frame=queue)
    result._set_voice_phase = nothing
    return result


@pytest.mark.asyncio
async def test_validated_ptt_boundaries_surround_exact_pcm_in_pinned_processor_queue():
    frames = []
    c = conversation(frames)
    await c.feed_audio(b'old')
    await c.capture_started()
    await c.capture_started()
    await c.feed_audio(b'\x01\0'*320)
    await c.feed_audio(b'\x02\0'*17)
    await c.capture_completed()
    await c.capture_completed()
    await c.feed_audio(b'late')
    queue = FrameProcessorQueue()
    for frame in frames:
        await queue.put((frame, FrameDirection.DOWNSTREAM, None))
    ordered = [(await queue.get())[0] for _ in frames]
    assert [type(frame) for frame in ordered] == [VADUserStartedSpeakingFrame,
        InputAudioRawFrame, InputAudioRawFrame, VADUserStoppedSpeakingFrame]
    assert b''.join(frame.audio for frame in ordered if isinstance(frame, InputAudioRawFrame)) == b'\x01\0'*320+b'\x02\0'*17


@pytest.mark.asyncio
async def test_cancel_does_not_commit_abandoned_audio_and_late_end_cannot_reopen_it():
    frames = []
    c = conversation(frames)
    await c.capture_started()
    await c.feed_audio(b'\1\0'*160)
    await c.cancel()
    await c.capture_completed()
    await c.feed_audio(b'late')
    assert [type(frame) for frame in frames] == [VADUserStartedSpeakingFrame, InputAudioRawFrame, InterruptionFrame]
    await c.capture_started()
    await c.feed_audio(b'\2\0'*160)
    await c.capture_completed()
    assert [type(frame) for frame in frames[-3:]] == [VADUserStartedSpeakingFrame, InputAudioRawFrame, VADUserStoppedSpeakingFrame]


@pytest.mark.asyncio
@pytest.mark.parametrize('explicit,override,expected', [
    (True, None, None), (False, None, 'near_field'),
    (True, 'far_field', 'far_field'), (False, 'off', None),
])
async def test_stt_profile_preserves_webrtc_and_allows_explicit_moq_filter(monkeypatch, explicit, override, expected):
    """Inspect the actual service boundary without starting provider sockets."""
    for key in ('OPENAI_API_KEY', 'ELEVENLABS_API_KEY', 'ELEVENLABS_DEFAULT_VOICE_ID'):
        monkeypatch.setenv(key, 'test-only')
    monkeypatch.delenv('DOODAD_STT_NOISE_REDUCTION', raising=False)
    if override is not None:
        monkeypatch.setenv('DOODAD_STT_NOISE_REDUCTION', override)
    observed = {}
    class ReachedSTT(Exception): pass
    class InspectSTT:
        Settings = conversation_module.OpenAIRealtimeSTTService.Settings
        def __init__(self, **kwargs):
            observed['filter'] = kwargs['settings'].noise_reduction
            observed['server_vad'] = kwargs['turn_detection']
            raise ReachedSTT()
    monkeypatch.setattr(conversation_module, 'OpenAIRealtimeSTTService', InspectSTT)
    monkeypatch.setattr(conversation_module, 'CaptureRealtimeSTTService', InspectSTT)
    monkeypatch.setattr(conversation_module, 'TracedSileroVADAnalyzer', lambda *a, **kw: None)
    monkeypatch.setattr(conversation_module, 'VADProcessor', lambda **kw: None)
    c = conversation([])
    c.explicit_capture = explicit
    c._tools = lambda: []
    with pytest.raises(ReachedSTT):
        await c.start()
    assert observed == {'filter': expected, 'server_vad': False}
