"""Cancellation at real frame, tool, history and provider-context boundaries."""
import asyncio
import base64
import json
from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    BotStartedSpeakingFrame, BotStoppedSpeakingFrame, FunctionCallInProgressFrame,
    FunctionCallResultFrame, InterruptionFrame, LLMContextFrame,
    LLMFullResponseStartFrame, LLMTextFrame, TTSStartedFrame, TTSAudioRawFrame,
    TTSStoppedFrame, TTSTextFrame, TranscriptionFrame,
    LLMMessagesAppendFrame, LLMAssistantPushAggregationFrame,
)
from pipecat.processors.aggregators.llm_context import LLMContext
from pipecat.processors.frame_processor import FrameDirection, FrameProcessor
from pipecat.services.elevenlabs.tts import ElevenLabsTTSService
from pipecat.services.openai.responses.llm import OpenAIResponsesLLMService

from doodad_agent.capture_pipeline import (
    CaptureAggregatorPair, CaptureAssistantAggregator, CaptureResponsesLLMService,
    CaptureElevenLabsTTSService, provider_turn,
)
from doodad_agent.capture_stt import CaptureTurn, frame_turn
from doodad_agent.conversation import ConversationSink, LiveConversation, AssistantTextTap
from doodad_agent.metrics import LatencyTrace
from doodad_agent.session import ACTION_CURRENT

D = FrameDirection.DOWNSTREAM


async def nothing(*_, **__): pass


def conversation():
    c = LiveConversation(None, None, None, LatencyTrace(), lambda *_: 0,
                         nothing, nothing, nothing, nothing, nothing, nothing,
                         explicit_capture=True)
    c.worker = SimpleNamespace(queue_frame=nothing)
    c._set_voice_phase = nothing
    return c


@pytest.mark.asyncio
async def test_cancelled_model_tool_cannot_start_in_replacement_capture():
    c = conversation()
    await c.capture_started()
    token = provider_turn.set(c._capture_turn)
    calls = []
    async def handler(_): calls.append(True)
    try:
        await c.cancel()
        await c.capture_started()
        with pytest.raises(ConnectionError):
            await c._current_tool(handler)(None)
        assert not calls
    finally:
        provider_turn.reset(token)


@pytest.mark.asyncio
async def test_inflight_tool_result_cannot_reenter_model_after_cancel():
    c = conversation()
    await c.capture_started()
    entered, release = asyncio.Event(), asyncio.Event()
    results = []
    async def result_callback(value): results.append(value)
    async def handler(params):
        entered.set()
        await release.wait()
        await params.result_callback({'ok': True})
    params = SimpleNamespace(result_callback=result_callback)
    token = provider_turn.set(c._capture_turn)
    task = asyncio.create_task(c._current_tool(handler)(params))
    provider_turn.reset(token)
    await asyncio.wait_for(entered.wait(), 1)
    await c.cancel(); await c.capture_started()
    release.set()
    with pytest.raises(ConnectionError): await task
    assert not results
    assert params.result_callback is result_callback


@pytest.mark.asyncio
async def test_live_tool_callback_and_durable_work_are_preserved():
    c = conversation()
    await c.capture_started()
    durable, results = [], []
    async def callback(value): results.append(value)
    async def handler(params):
        durable.append('accepted-job')
        await params.result_callback({'ok': True})
    token = provider_turn.set(c._capture_turn)
    try:
        await c._current_tool(handler)(SimpleNamespace(result_callback=callback))
        await c.cancel()
        assert durable == ['accepted-job'] and results == [{'ok': True}]
    finally: provider_turn.reset(token)


@pytest.mark.asyncio
async def test_live_delayed_tool_callback_keeps_origin_outside_model_task():
    c = conversation()
    await c.capture_started()
    callbacks, owners = [], []
    async def handler(params): callbacks.append(params.result_callback)
    async def result(value): owners.append(provider_turn.get())
    token = provider_turn.set(c._capture_turn)
    try: await c._current_tool(handler)(SimpleNamespace(result_callback=result))
    finally: provider_turn.reset(token)
    assert provider_turn.get() is None
    await callbacks[0]({'ok':True})
    assert owners == [c._capture_turn] and provider_turn.get() is None


@pytest.mark.asyncio
async def test_cancel_during_device_stop_cannot_begin_new_downlink():
    c = conversation()
    await c.capture_started()
    entered, release = asyncio.Event(), asyncio.Event()
    calls = []
    async def stop():
        entered.set(); await release.wait()
    async def begin(): calls.append('begin')
    c.stop_capture, c.begin_downlink = stop, begin
    token = provider_turn.set(c._capture_turn)
    task = asyncio.create_task(c._begin_speaking())
    provider_turn.reset(token)
    await asyncio.wait_for(entered.wait(), 1)
    await c.cancel(); await c.capture_started()
    release.set(); await task
    assert not calls


@pytest.mark.asyncio
async def test_action_writer_guard_closes_over_origin_not_writer_context():
    c = conversation()
    await c.capture_started()
    guards = []
    async def action(*_):
        guards.append(ACTION_CURRENT.get())
        return {'ok': True}
    c.action_invoker = action
    token = provider_turn.set(c._capture_turn)
    try: assert await c._invoke_action('read', {}, 'request') == {'ok': True}
    finally: provider_turn.reset(token)
    assert guards[0]()  # The writer has no provider-turn ContextVar.
    await c.cancel(); await c.capture_started()
    assert not guards[0]()


def test_model_cache_cannot_reuse_cancelled_provider_output():
    llm = CaptureResponsesLLMService(api_key='test-only')
    turn = CaptureTurn()
    token = provider_turn.set(turn)
    try:
        llm._store_previous_response_state('synthetic-response', [], [])
    finally: provider_turn.reset(token)
    full = [{'role': 'user', 'content': 'synthetic'}]
    assert llm._apply_previous_response_optimization({'input':full}, full)['previous_response_id'] == 'synthetic-response'
    turn.live = False
    assert 'previous_response_id' not in llm._apply_previous_response_optimization({'input':full}, full)
    assert llm._previous_response_id is None


def sink_harness():
    writes, frames, pauses = [], [], []
    async def pause(): pauses.append(provider_turn.get())
    sink = ConversationSink(lambda pcm, rate: writes.append(len(pcm)), LatencyTrace(),
                            nothing, nothing, nothing, nothing, pause, require_capture=True)
    async def push(frame, direction=D): frames.append((frame, direction))
    sink.push_frame = push
    return sink, writes, frames, pauses


def tts_frames(turn, context='context'):
    return [turn.stamp(TTSStartedFrame(context_id=context)),
            turn.stamp(TTSAudioRawFrame(b'\0\0'*160, 16000, 1, context_id=context)),
            turn.stamp(TTSTextFrame('synthetic speech', aggregated_by='sentence', context_id=context)),
            turn.stamp(TTSStoppedFrame(context_id=context))]


@pytest.mark.asyncio
async def test_late_tts_start_cannot_reactivate_cancelled_audio():
    sink, writes, frames, pauses = sink_harness()
    for frame in tts_frames(CaptureTurn(live=False)):
        await sink.process_frame(frame, D)
    assert not writes and not frames and not pauses
    assert not sink._tts_active


@pytest.mark.asyncio
async def test_live_audio_and_playout_keep_origin_on_derived_bot_frames():
    sink, writes, frames, pauses = sink_harness()
    turn = CaptureTurn()
    for frame in tts_frames(turn): await sink.process_frame(frame, D)
    assert writes == [320] and pauses == [turn]
    bots = [f for f, _ in frames if isinstance(f, (BotStartedSpeakingFrame, BotStoppedSpeakingFrame))]
    assert len(bots) == 4 and all(frame_turn(f) is turn for f in bots)
    assert any(isinstance(f, TTSTextFrame) for f, _ in frames)


@pytest.mark.asyncio
async def test_cancel_during_playout_drain_discards_unheard_history_and_pause():
    sink, writes, frames, pauses = sink_harness()
    entered, release = asyncio.Event(), asyncio.Event()
    async def drain(): entered.set(); await release.wait()
    sink.on_playout_drain = drain
    turn = CaptureTurn()
    start, audio, text, stop = tts_frames(turn)
    for frame in (start, audio, text): await sink.process_frame(frame, D)
    task = asyncio.create_task(sink.process_frame(stop, D))
    await asyncio.wait_for(entered.wait(), 1)
    turn.live = False
    release.set(); await task
    assert not any(isinstance(f, TTSTextFrame) for f, _ in frames)
    assert not pauses and not sink._pending_tts_text


@pytest.mark.asyncio
@pytest.mark.parametrize('cancel', [False, True])
async def test_standalone_speech_history_flush_waits_for_played_words(cancel):
    sink, _, frames, _ = sink_harness()
    entered, release = asyncio.Event(), asyncio.Event()
    async def drain(): entered.set(); await release.wait()
    sink.on_playout_drain = drain
    turn = CaptureTurn()
    start, audio, text, stop = tts_frames(turn)
    flush = turn.stamp(LLMAssistantPushAggregationFrame())
    # The pinned TTS service emits the flush BEFORE TTSStoppedFrame. Words are
    # still held by our transport sink until the watch's playout receipt.
    for frame in (start, audio, text, flush): await sink.process_frame(frame, D)
    assert not any(isinstance(f, LLMAssistantPushAggregationFrame) for f, _ in frames)
    task = asyncio.create_task(sink.process_frame(stop, D))
    await asyncio.wait_for(entered.wait(), 1)
    if cancel: turn.live = False
    release.set()
    await task
    history_frames = [f for f, _ in frames if isinstance(f, (TTSTextFrame, LLMAssistantPushAggregationFrame))]
    assert history_frames == ([] if cancel else [text, flush])


@pytest.mark.asyncio
async def test_cancel_during_bot_start_prevents_audio_write():
    sink, writes, _, _ = sink_harness()
    turn = CaptureTurn()
    async def push(frame, direction=D):
        if isinstance(frame, BotStartedSpeakingFrame): turn.live = False
    sink.push_frame = push
    start, audio, _, _ = tts_frames(turn)
    await sink.process_frame(start, D)
    await sink.process_frame(audio, D)
    assert not writes


@pytest.mark.asyncio
async def test_cancelled_model_text_cannot_reset_or_append_response_journal():
    calls = []
    async def reset(): calls.append('reset')
    async def text(value): calls.append(value)
    tap = AssistantTextTap(reset, text, require_capture=True)
    tap.push_frame = nothing
    old = CaptureTurn(live=False)
    for frame in [old.stamp(LLMFullResponseStartFrame()), old.stamp(LLMTextFrame('synthetic'))]:
        await tap.process_frame(frame, D)
    assert not calls


@pytest.mark.asyncio
async def test_model_request_rechecks_capture_after_connection_wait(monkeypatch):
    sent = []
    async def send(self, msg): sent.append(msg['type'])
    monkeypatch.setattr(OpenAIResponsesLLMService, '_ws_send', send)
    llm = CaptureResponsesLLMService(api_key='test-only')
    turn = CaptureTurn()
    token = provider_turn.set(turn)
    try:
        await llm._ws_send({'type': 'response.create'})
        turn.live = False
        with pytest.raises(ConnectionError): await llm._ws_send({'type': 'response.create'})
        await llm._ws_send({'type': 'response.cancel'})
        assert sent == ['response.create', 'response.cancel']
    finally: provider_turn.reset(token)


@pytest.mark.asyncio
async def test_model_cancel_after_send_uses_inherited_cancel_and_drain_signal(monkeypatch):
    turn = CaptureTurn()
    async def send(self, message): turn.live = False
    monkeypatch.setattr(OpenAIResponsesLLMService, '_ws_send', send)
    llm = CaptureResponsesLLMService(api_key='test-only')
    token = provider_turn.set(turn)
    try:
        with pytest.raises(asyncio.CancelledError): await llm._ws_send({'type':'response.create'})
    finally: provider_turn.reset(token)


@pytest.mark.asyncio
@pytest.mark.parametrize('kind', ['response.created', 'response.completed'])
async def test_model_event_already_received_is_not_lost_during_cancellation(monkeypatch, kind):
    turn = CaptureTurn()
    event = {'type':kind, 'response':{'id':'synthetic-response'}}
    async def receive(self):
        turn.live = False
        return event
    monkeypatch.setattr(OpenAIResponsesLLMService, '_ws_recv', receive)
    llm = CaptureResponsesLLMService(api_key='test-only')
    token = provider_turn.set(turn)
    try:
        assert await llm._ws_recv() is event
        with pytest.raises(asyncio.CancelledError): await llm._ws_recv()
    finally: provider_turn.reset(token)


@pytest.mark.asyncio
async def test_stale_reasoning_context_append_cannot_mutate_history(monkeypatch):
    monkeypatch.setattr(FrameProcessor, 'process_frame', nothing)
    monkeypatch.setattr(FrameProcessor, 'push_frame', nothing)
    context = LLMContext([])
    aggregator = CaptureAssistantAggregator(context)
    frame = CaptureTurn(live=False).stamp(LLMMessagesAppendFrame(
        [{'role':'assistant', 'content':'synthetic stale reasoning'}]))
    await aggregator.process_frame(frame, D)
    assert not context.get_messages()


@pytest.mark.asyncio
async def test_sequential_tool_runner_does_not_use_new_task_context(monkeypatch):
    admitted = []
    async def run(self, item): admitted.append(provider_turn.get())
    monkeypatch.setattr(OpenAIResponsesLLMService, '_run_function_call', run)
    llm = CaptureResponsesLLMService(api_key='test-only')
    old, new = CaptureTurn(live=False), CaptureTurn()
    token = provider_turn.set(new)
    try:
        await llm._run_function_call(SimpleNamespace(voicewatch_capture=old))
        await llm._run_function_call(SimpleNamespace(voicewatch_capture=new))
        assert admitted == [new]
    finally: provider_turn.reset(token)


@pytest.mark.asyncio
async def test_assistant_rejects_stale_tool_result_without_history_mutation(monkeypatch):
    monkeypatch.setattr(FrameProcessor, 'process_frame', nothing)
    monkeypatch.setattr(FrameProcessor, 'push_frame', nothing)
    context = LLMContext([])
    aggregator = CaptureAssistantAggregator(context)
    turn = CaptureTurn()
    await aggregator.process_frame(turn.stamp(FunctionCallInProgressFrame(
        function_name='get_next_set', tool_call_id='call', arguments={}, cancel_on_interruption=True)), D)
    before = json.dumps(context.get_messages())
    turn.live = False
    await aggregator.process_frame(turn.stamp(FunctionCallResultFrame(
        function_name='get_next_set', tool_call_id='call', arguments={}, result={'ok': True})), D)
    assert json.dumps(context.get_messages()) == before
    await aggregator.process_frame(InterruptionFrame(), D)
    assert not aggregator._function_calls_in_progress
    assert context.get_messages()[-1]['content'].startswith('INTERRUPTED;')


@pytest.mark.asyncio
async def test_live_tool_result_emits_followup_context_with_same_owner(monkeypatch):
    monkeypatch.setattr(FrameProcessor, 'process_frame', nothing)
    frames = []
    async def push(self, frame, direction=D): frames.append((frame, direction))
    monkeypatch.setattr(FrameProcessor, 'push_frame', push)
    context = LLMContext([])
    aggregator = CaptureAssistantAggregator(context)
    turn = CaptureTurn()
    await aggregator.process_frame(turn.stamp(FunctionCallInProgressFrame(
        function_name='get_next_set', tool_call_id='call', arguments={}, cancel_on_interruption=True)), D)
    await aggregator.process_frame(turn.stamp(FunctionCallResultFrame(
        function_name='get_next_set', tool_call_id='call', arguments={}, result={'ok': True})), D)
    followups = [f for f, direction in frames if isinstance(f, LLMContextFrame) and direction == FrameDirection.UPSTREAM]
    assert len(followups) == 1 and frame_turn(followups[0]) is turn
    assert json.loads(context.get_messages()[-1]['content']) == {'ok': True}


@pytest.mark.asyncio
async def test_user_aggregation_emits_original_capture_and_rejects_old_timer(monkeypatch):
    monkeypatch.setattr(FrameProcessor, 'process_frame', nothing)
    frames = []
    async def push(self, frame, direction=D): frames.append(frame)
    monkeypatch.setattr(FrameProcessor, 'push_frame', push)
    context = LLMContext([])
    user = CaptureAggregatorPair(context).user()
    # Exercise actual aggregation/context emission without starting turn-strategy timers.
    user._user_turn_controller.process_frame = nothing
    user._user_idle_controller.process_frame = nothing
    old, new = CaptureTurn(), CaptureTurn()
    await user.process_frame(old.stamp(TranscriptionFrame('first synthetic','test','')), D)
    await user.push_aggregation()
    assert frame_turn(next(f for f in frames if isinstance(f, LLMContextFrame))) is old
    old.live = False
    await user.process_frame(new.stamp(TranscriptionFrame('second synthetic','test','')), D)
    token = user._origin.set(old)
    try: assert await user.push_aggregation() == ''
    finally: user._origin.reset(token)
    assert len(context.get_messages()) == 1
    await user.push_aggregation()
    assert len(context.get_messages()) == 2
    assert frame_turn([f for f in frames if isinstance(f, LLMContextFrame)][-1]) is new


def tts_service():
    tts = CaptureElevenLabsTTSService(api_key='test-only',
        settings=ElevenLabsTTSService.Settings(voice='test-only'), sample_rate=16000)
    tts._audio_contexts = {}
    tts._serialization_queue = asyncio.Queue()
    return tts


@pytest.mark.asyncio
async def test_tts_context_cannot_be_recreated_by_late_provider_audio():
    tts = tts_service()
    turn = CaptureTurn()
    token = tts._origin.set(turn)
    try: context_id = tts.create_context_id()
    finally: tts._origin.reset(token)
    tts._turn_context_id = context_id
    turn.live = False
    await tts.append_to_audio_context(context_id, TTSAudioRawFrame(b'\0\0',16000,1))
    assert not tts._audio_contexts


@pytest.mark.asyncio
async def test_tts_filters_stale_alignment_before_inherited_receiver(monkeypatch):
    tts = tts_service()
    old, new = CaptureTurn(live=False), CaptureTurn()
    tts._capture_contexts = {'old': old, 'new': new}
    class Socket:
        async def __aiter__(self):
            for context_id in ['old','unknown','new']:
                yield json.dumps({'contextId':context_id,'alignment':{'chars':['synthetic']}})
    tts._websocket = Socket()
    events = [json.loads(raw) async for raw in tts._get_websocket()]
    assert [e['contextId'] for e in events] == ['new']


@pytest.mark.asyncio
async def test_cancel_during_received_audio_cannot_modify_replacement_alignment():
    tts = tts_service()
    old, new = CaptureTurn(), CaptureTurn()
    tts._capture_contexts = {'old': old, 'new': new}
    entered, release = asyncio.Event(), asyncio.Event()
    async def append(context_id, frame):
        entered.set(); await release.wait()
    tts.append_to_audio_context = append
    event = {'contextId':'old', 'audio':base64.b64encode(b'\0\0').decode(),
             'alignment':{'chars':['x',' '], 'charStartTimesMs':[0,10], 'charDurationsMs':[10,10]}}
    task = asyncio.create_task(tts._receive_context_event(event))
    await asyncio.wait_for(entered.wait(), 1)
    old.live = False
    tts._capture_alignment['new'] = (1.0, 'new-word', 1.0)
    release.set(); await task
    assert tts._capture_alignment == {'new':(1.0, 'new-word', 1.0)}


@pytest.mark.asyncio
async def test_interleaved_live_contexts_do_not_share_partial_words():
    tts = tts_service()
    tts._capture_contexts = {'one':CaptureTurn(), 'two':CaptureTurn()}
    words = []
    async def add(value, context, **kwargs): words.append((context, value))
    tts.add_word_timestamps = add
    def event(context, chars):
        return {'contextId':context, 'alignment':{'chars':list(chars),
            'charStartTimesMs':list(range(0,len(chars)*10,10)), 'charDurationsMs':[10]*len(chars)}}
    await tts._receive_context_event(event('one','first'))
    await tts._receive_context_event(event('two','second '))
    await tts._receive_context_event(event('one',' '))
    assert words[0][0] == 'two' and words[0][1][0][0] == 'second'
    assert words[1][0] == 'one' and words[1][1][0][0] == 'first'


@pytest.mark.asyncio
async def test_tts_cancellation_at_generator_yield_prevents_context_init(monkeypatch):
    tts = tts_service()
    turn = CaptureTurn()
    tts._capture_contexts = {'context':turn}
    writes = []
    async def generate(self, text, context_id):
        yield TTSStartedFrame(context_id=context_id)
        writes.append('init-and-speech')
        yield None
    monkeypatch.setattr(ElevenLabsTTSService, 'run_tts', generate)
    generator = tts.run_tts('synthetic', 'context')
    assert isinstance(await anext(generator), TTSStartedFrame)
    turn.live = False
    with pytest.raises(StopAsyncIteration): await anext(generator)
    assert not writes
