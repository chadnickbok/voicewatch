from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    InputAudioRawFrame, InterruptionFrame, VADUserStartedSpeakingFrame,
    VADUserStoppedSpeakingFrame,
)
from pipecat.processors.frame_processor import FrameDirection, FrameProcessorQueue

from doodad_agent.conversation import CaptureBoundaryProcessor, LiveConversation
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
async def test_new_capture_clears_abandoned_provider_buffer_before_its_first_audio():
    events = []
    async def clear(): events.append('clear')
    processor = CaptureBoundaryProcessor(clear)
    async def push(frame, direction): events.append(type(frame).__name__)
    processor.push_frame = push
    for frame in [VADUserStartedSpeakingFrame(), InputAudioRawFrame(b'\1\0'*160,16000,1),
                  VADUserStoppedSpeakingFrame(), VADUserStartedSpeakingFrame()]:
        await processor.process_frame(frame, FrameDirection.DOWNSTREAM)
    assert events == ['clear','VADUserStartedSpeakingFrame','InputAudioRawFrame',
        'VADUserStoppedSpeakingFrame','clear','VADUserStartedSpeakingFrame']
