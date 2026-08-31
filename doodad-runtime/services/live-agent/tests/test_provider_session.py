from __future__ import annotations

import asyncio
from types import SimpleNamespace

import pytest
from pipecat.frames.frames import TTSAudioRawFrame, TTSStartedFrame, TTSStoppedFrame, TTSTextFrame
from pipecat.processors.frame_processor import FrameDirection

from doodad_agent.conversation import ConversationSink, LiveConversation
from doodad_agent.metrics import LatencyTrace
from doodad_agent.provider_session import ProviderSession


class Session:
    def __init__(self):
        self.calls = []
        self._closed = False
        self.release = asyncio.Event()

    def begin_downlink(self): self.calls.append('begin')
    def enqueue_downlink(self, pcm, rate): self.calls.append('audio'); return len(pcm)//2
    def end_downlink(self): self.calls.append('end'); return 0
    def clear_downlink(self): self.calls.append('clear')
    async def resume_after_downlink(self): await self.release.wait()
    async def stop_capture(self): self.calls.append('stop')
    async def invoke_action(self, *args):
        self.calls.append('action')
        await self.release.wait()
        return {'ok': True}


@pytest.mark.asyncio
async def test_delayed_provider_callbacks_never_route_to_replacement():
    old, new = Session(), Session()
    current = [old]
    owner = ProviderSession(old, lambda: current[0])
    await owner.begin()
    assert owner.audio(b'\0\0', 16000) == 1
    current[0] = new
    await owner.begin(); await owner.end(); await owner.stop_capture(); await owner.wait()
    assert owner.audio(b'\0\0', 16000) == 0
    with pytest.raises(ConnectionError):
        await owner.action('log_food', {}, 'old')
    owner.retire()
    assert new.calls == []
    assert old.calls == ['begin', 'audio', 'clear']


@pytest.mark.asyncio
async def test_old_action_result_and_drain_cannot_finish_new_session():
    old, new = Session(), Session()
    current = [old]
    owner = ProviderSession(old, lambda: current[0])
    await owner.begin()
    drain = asyncio.create_task(owner.wait())
    action = asyncio.create_task(owner.action('get_next_set', {}, 'first'))
    await asyncio.sleep(0)
    current[0] = new
    replacement = ProviderSession(new, lambda: current[0])
    await replacement.begin()
    old.release.set()
    await drain
    with pytest.raises(ConnectionError): await action
    assert replacement.audio(b'\0\0', 16000) == 1
    assert new.calls == ['begin', 'audio']


async def nothing(): pass


@pytest.mark.asyncio
async def test_sink_rejects_retired_provider_context_audio_text_and_stop():
    output, pushed = [], []
    sink = ConversationSink(lambda pcm, rate: output.append(pcm), LatencyTrace(),
                            nothing, nothing, nothing, nothing, nothing)
    async def push(frame, direction=FrameDirection.DOWNSTREAM): pushed.append(frame)
    sink.push_frame = push
    direction = FrameDirection.DOWNSTREAM
    await sink.process_frame(TTSStartedFrame(context_id='current'), direction)
    await sink.process_frame(TTSAudioRawFrame(b'old', 16000, 1, context_id='old'), direction)
    await sink.process_frame(TTSTextFrame('old words', aggregated_by='sentence', context_id='old'), direction)
    await sink.process_frame(TTSStoppedFrame(context_id='old'), direction)
    assert not output and sink._tts_active
    await sink.process_frame(TTSAudioRawFrame(b'good', 16000, 1, context_id='current'), direction)
    assert output == [b'good']
    sink.retire()
    await sink.process_frame(TTSStartedFrame(context_id='late'), direction)
    await sink.process_frame(TTSAudioRawFrame(b'late', 16000, 1, context_id='late'), direction)
    assert output == [b'good'] and not sink._tts_active


@pytest.mark.asyncio
async def test_old_drain_cannot_clear_new_context_text():
    draining, release = asyncio.Event(), asyncio.Event()
    async def drain(): draining.set(); await release.wait()
    sink = ConversationSink(lambda *_: 0, LatencyTrace(), nothing, nothing, nothing, drain, nothing)
    async def push(*_): pass
    sink.push_frame = push
    direction = FrameDirection.DOWNSTREAM
    await sink.process_frame(TTSStartedFrame(context_id='old'), direction)
    stopped = asyncio.create_task(sink.process_frame(TTSStoppedFrame(context_id='old'), direction))
    await draining.wait()
    await sink.process_frame(TTSStartedFrame(context_id='new'), direction)
    await sink.process_frame(TTSTextFrame('new words', aggregated_by='sentence', context_id='new'), direction)
    release.set(); await stopped
    assert [frame.text for frame, _ in sink._pending_tts_text] == ['new words']


@pytest.mark.asyncio
async def test_disconnect_cancels_provider_worker_but_preserves_durable_builder():
    calls = []
    async def cancelled(**_): calls.append('worker.cancel')
    conversation = LiveConversation(None, SimpleNamespace(close=lambda: calls.append('builder.close')),
        None, LatencyTrace(), lambda *_: 0, nothing, nothing, nothing, nothing, nothing, nothing)
    conversation.worker = SimpleNamespace(cancel=cancelled)
    conversation.disconnected()
    assert conversation._retired
    await conversation.close(close_builder=False)
    assert calls == ['worker.cancel']
    with pytest.raises(ConnectionError):
        await conversation._current_tool(nothing)(None)
