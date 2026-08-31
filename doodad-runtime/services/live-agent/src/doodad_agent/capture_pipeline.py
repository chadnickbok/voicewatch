"""Carry admitted capture ownership through the persistent provider pipeline.

Owners come from stamped input/context frames or a registered TTS context,
never from whichever capture happens to be current when a callback arrives.
These adapters extend the pinned Pipecat services without editing dependencies.

Only the explicit-capture MoQ path uses these adapters. The WebRTC path retains
its existing provider services and aggregators.
"""
from __future__ import annotations

import asyncio
import base64
import json
from contextvars import ContextVar

from pipecat.frames.frames import (
    AggregatedTextProgressFrame, BotStartedSpeakingFrame, BotStoppedSpeakingFrame,
    FunctionCallCancelFrame, FunctionCallInProgressFrame, FunctionCallResultFrame,
    FunctionCallsStartedFrame, InputAudioRawFrame, InterimTranscriptionFrame,
    InterruptionFrame, LLMAssistantPushAggregationFrame, LLMContextFrame,
    LLMFullResponseEndFrame, LLMFullResponseStartFrame, LLMThoughtEndFrame,
    LLMThoughtStartFrame, LLMThoughtTextFrame, TextFrame, TranscriptionFrame,
    LLMMessagesAppendFrame, LLMMessagesUpdateFrame, LLMMessagesTransformFrame,
    LLMRunFrame, LLMMarkerFrame,
    TTSAudioRawFrame, TTSSpeakFrame, TTSStartedFrame, TTSStoppedFrame,
    UserStartedSpeakingFrame, UserStoppedSpeakingFrame,
    VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame,
)
from pipecat.processors.aggregators.llm_response_universal import (
    LLMAssistantAggregator, LLMUserAggregator, LLMUserAggregatorParams,
)
from pipecat.processors.frame_processor import FrameDirection
from pipecat.services.elevenlabs.tts import (
    ElevenLabsTTSService, calculate_word_times, _select_alignment,
    _strip_utterance_leading_spaces, _word_timestamps_include_inter_frame_spaces,
)
from pipecat.services.settings import assert_given
from pipecat.services.openai.responses.llm import OpenAIResponsesLLMService

from .capture_stt import CaptureTurn, frame_turn


provider_turn: ContextVar[CaptureTurn | None] = ContextVar("provider_turn", default=None)

OWNED_FRAMES = (
    TextFrame, LLMContextFrame, LLMFullResponseStartFrame, LLMFullResponseEndFrame,
    LLMThoughtStartFrame, LLMThoughtTextFrame, LLMThoughtEndFrame,
    FunctionCallsStartedFrame, FunctionCallInProgressFrame, FunctionCallResultFrame,
    FunctionCallCancelFrame, TTSSpeakFrame, TTSStartedFrame, TTSStoppedFrame,
    TTSAudioRawFrame, LLMAssistantPushAggregationFrame, AggregatedTextProgressFrame,
    UserStartedSpeakingFrame, UserStoppedSpeakingFrame, BotStartedSpeakingFrame,
    BotStoppedSpeakingFrame,
    LLMMessagesAppendFrame, LLMMessagesUpdateFrame, LLMMessagesTransformFrame,
    LLMRunFrame, LLMMarkerFrame,
)


def live(turn):
    return turn is not None and turn.live


class CaptureUserAggregator(LLMUserAggregator):
    """Retain the owner of aggregated input when a turn strategy emits context."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._aggregation_turn = None
        self._origin = ContextVar("user_aggregation_origin", default=None)

    async def process_frame(self, frame, direction):
        turn = frame_turn(frame)
        if isinstance(frame, OWNED_FRAMES) and not live(turn):
            return
        if isinstance(frame, (InputAudioRawFrame, TranscriptionFrame, InterimTranscriptionFrame,
                              VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame)):
            if not live(turn):
                return
            if isinstance(frame, (TranscriptionFrame, VADUserStartedSpeakingFrame)):
                if self._aggregation_turn is not turn:
                    await self.reset()
                self._aggregation_turn = turn
        token = self._origin.set(turn)
        try:
            await super().process_frame(frame, direction)
        finally:
            self._origin.reset(token)

    async def push_aggregation(self):
        origin = self._origin.get()
        turn = self._aggregation_turn
        # A late timer from an old turn cannot flush a newer turn's input.
        if not live(turn) or (origin is not None and origin is not turn):
            return ""
        token = self._origin.set(turn)
        try:
            return await super().push_aggregation()
        finally:
            self._origin.reset(token)

    async def push_frame(self, frame, direction=FrameDirection.DOWNSTREAM):
        if isinstance(frame, OWNED_FRAMES):
            turn = frame_turn(frame) or self._origin.get()
            if not live(turn):
                return
            turn.stamp(frame)
        await super().push_frame(frame, direction)


class CaptureAssistantAggregator(LLMAssistantAggregator):
    """Fence history mutations and retain ownership of tool-result follow-ups."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._aggregation_turn = None
        self._origin = ContextVar("assistant_aggregation_origin", default=None)

    async def process_frame(self, frame, direction):
        turn = frame_turn(frame)
        if isinstance(frame, OWNED_FRAMES) and not live(turn):
            return
        if isinstance(frame, (TextFrame, LLMFullResponseStartFrame)):
            if self._aggregation_turn is not turn:
                await self.reset()
            self._aggregation_turn = turn
        token = self._origin.set(turn)
        try:
            await super().process_frame(frame, direction)
        finally:
            self._origin.reset(token)

    async def push_aggregation(self):
        turn = self._aggregation_turn
        if not live(turn):
            await self.reset()
            return ""
        token = self._origin.set(turn)
        try:
            return await super().push_aggregation()
        finally:
            self._origin.reset(token)

    async def push_frame(self, frame, direction=FrameDirection.DOWNSTREAM):
        if isinstance(frame, OWNED_FRAMES):
            turn = frame_turn(frame) or self._origin.get()
            if not live(turn):
                return
            turn.stamp(frame)
        await super().push_frame(frame, direction)

    async def _handle_interruptions(self, frame):
        self._bot_speaking = False
        self._push_context_on_bot_stopped_speaking = False
        # Cancel frames may themselves arrive after capture retirement. Clean
        # only retired entries here so they cannot hold up the next tool round.
        # An action already dispatched to the watch may have committed there;
        # do not turn missing acknowledgement into a claim it did not execute.
        for call_id, call in list(self._function_calls_in_progress.items()):
            if call is None or not live(frame_turn(call)):
                if call is not None:
                    self._update_function_call_result(
                        call.function_name, call_id,
                        "INTERRUPTED; any already-issued action may have completed",
                    )
                self._function_calls_in_progress.pop(call_id, None)
                self._function_calls_image_results.pop(call_id, None)
        await super()._handle_interruptions(frame)


class CaptureAggregatorPair:
    def __init__(self, context, *, user_params=None):
        self._user = CaptureUserAggregator(context, params=user_params or LLMUserAggregatorParams(),
                                          _realtime_service_mode=False)
        self._assistant = CaptureAssistantAggregator(context, _realtime_service_mode=False,
                                                     _paired_user_aggregator=self._user)

    def user(self): return self._user
    def assistant(self): return self._assistant


class CaptureResponsesLLMService(OpenAIResponsesLLMService):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._response_owner = None

    def _store_previous_response_state(self, response_id, full_input, response_output):
        if live(provider_turn.get()):
            self._response_owner = provider_turn.get()
            super()._store_previous_response_state(response_id, full_input, response_output)

    def _apply_previous_response_optimization(self, params, full_input):
        if not live(self._response_owner):
            # Provider-side output can include words never played on the watch.
            # Rebuild from admitted local history, keeping the socket open.
            self._clear_previous_response_state()
        return super()._apply_previous_response_optimization(params, full_input)

    async def _append_reasoning_message(self, *args, **kwargs):
        if live(provider_turn.get()):
            await super()._append_reasoning_message(*args, **kwargs)

    async def _ws_send(self, message):
        if message.get("type") == "response.create" and not live(provider_turn.get()):
            raise ConnectionError("model capture retired before request")
        await super()._ws_send(message)
        if message.get("type") == "response.create" and not live(provider_turn.get()):
            # A request was sent; let the pinned service cancel and drain it.
            raise asyncio.CancelledError()

    async def _ws_recv(self):
        if not live(provider_turn.get()):
            raise asyncio.CancelledError()
        # Do not discard an event already read during cancellation. In
        # particular, losing response.created/completed would break the pinned
        # service's cancel-and-drain state. Outgoing frames, history and tool
        # dispatch have their own fences after asynchronous event processing.
        return await super()._ws_recv()

    async def process_frame(self, frame, direction):
        turn = frame_turn(frame)
        if isinstance(frame, OWNED_FRAMES) and not live(turn):
            return
        token = provider_turn.set(turn)
        try:
            await super().process_frame(frame, direction)
        finally:
            provider_turn.reset(token)

    async def _process_context(self, context):
        if live(provider_turn.get()):
            await super()._process_context(context)

    async def push_frame(self, frame, direction=FrameDirection.DOWNSTREAM):
        if isinstance(frame, OWNED_FRAMES):
            turn = frame_turn(frame) or provider_turn.get()
            if not live(turn):
                return
            turn.stamp(frame)
        await super().push_frame(frame, direction)

    async def run_function_calls(self, function_calls):
        if live(provider_turn.get()):
            await super().run_function_calls(function_calls)

    async def _run_parallel_function_calls(self, runner_items):
        for item in runner_items:
            item.voicewatch_capture = provider_turn.get()
        await super()._run_parallel_function_calls(runner_items)

    async def _run_sequential_function_calls(self, runner_items):
        for item in runner_items:
            item.voicewatch_capture = provider_turn.get()
        await super()._run_sequential_function_calls(runner_items)

    async def _run_function_call(self, runner_item):
        turn = getattr(runner_item, "voicewatch_capture", None)
        if not live(turn):
            return
        token = provider_turn.set(turn)
        try:
            await super()._run_function_call(runner_item)
        finally:
            provider_turn.reset(token)


class _ContextMessages:
    """Filter provider contexts before the base service changes word alignment."""

    def __init__(self, socket, context_live):
        self.socket, self.context_live = socket, context_live

    async def __aiter__(self):
        async for message in self.socket:
            event = json.loads(message)
            if isinstance(event, dict) and self.context_live(event.get("contextId")):
                yield message


class CaptureElevenLabsTTSService(ElevenLabsTTSService):
    MAX_CONTEXTS = 32

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._capture_contexts = {}
        self._capture_alignment = {}
        self._origin = ContextVar("tts_capture_origin", default=None)

    def _context_live(self, context_id):
        return isinstance(context_id, str) and live(self._capture_contexts.get(context_id))

    async def process_frame(self, frame, direction):
        turn = frame_turn(frame)
        if isinstance(frame, OWNED_FRAMES) and not live(turn):
            return
        token = self._origin.set(turn)
        try:
            await super().process_frame(frame, direction)
        finally:
            self._origin.reset(token)

    def create_context_id(self):
        turn = self._origin.get()
        if not live(turn):
            raise RuntimeError("TTS capture retired")
        context_id = super().create_context_id()
        self._capture_contexts = {key: owner for key, owner in self._capture_contexts.items() if owner.live}
        self._capture_alignment = {key: state for key, state in self._capture_alignment.items()
                                   if key in self._capture_contexts}
        if context_id not in self._capture_contexts and len(self._capture_contexts) >= self.MAX_CONTEXTS:
            raise RuntimeError("TTS capture context capacity")
        previous = self._capture_contexts.get(context_id)
        if previous is not None and previous is not turn:
            raise RuntimeError("TTS context owner changed")
        self._capture_contexts[context_id] = turn
        return context_id

    def _get_websocket(self):
        return _ContextMessages(super()._get_websocket(), self._context_live)

    async def _receive_messages(self):
        async for message in self._get_websocket():
            await self._receive_context_event(json.loads(message))

    async def _receive_context_event(self, event):
        """Use the pinned alignment helpers with state owned by each context.

        The base receiver uses shared partial-word/time fields and awaits audio
        delivery before updating them. A cancellation during that await must
        not let an old event modify a replacement context's word alignment.
        """
        context_id = event.get('contextId')
        if not self._context_live(context_id):
            return
        if event.get('isFinal') is True:
            if self.audio_context_available(context_id):
                await self.append_to_audio_context(context_id, TTSStoppedFrame(context_id=context_id))
                await self.remove_audio_context(context_id)
            return
        if event.get('audio'):
            audio = base64.b64decode(event['audio'])
            await self.append_to_audio_context(context_id,
                TTSAudioRawFrame(audio, self.sample_rate, 1, context_id=context_id))
        if not self._context_live(context_id):
            return
        raw = _select_alignment(event, normalized_key='normalizedAlignment', alignment_key='alignment',
                                prefer_normalized=bool(self._pronunciation_dictionary_locators))
        if not raw:
            return
        previous = self._capture_alignment.get(context_id)
        cumulative, partial, partial_start = previous or (0.0, '', 0.0)
        alignment = _strip_utterance_leading_spaces(raw,
            ('chars', 'charStartTimesMs', 'charDurationsMs'), previous is None)
        words, partial, partial_start = calculate_word_times(alignment, cumulative, partial, partial_start)
        if words:
            starts, durations = alignment.get('charStartTimesMs', []), alignment.get('charDurationsMs', [])
            cumulative = cumulative + (starts[-1] + durations[-1]) / 1000 if starts and durations else words[-1][1]
        self._capture_alignment[context_id] = (cumulative, partial, partial_start)
        if words:
            await self.add_word_timestamps(words, context_id,
                includes_inter_frame_spaces=True if _word_timestamps_include_inter_frame_spaces(
                    assert_given(self._settings.language)) else None)

    async def append_to_audio_context(self, context_id, frame):
        if frame is not None and not self._context_live(context_id):
            return
        if frame is not None and hasattr(frame, "metadata"):
            self._capture_contexts[context_id].stamp(frame)
        await super().append_to_audio_context(context_id, frame)

    async def create_audio_context(self, context_id):
        if not self._context_live(context_id):
            raise RuntimeError("TTS capture retired")
        await super().create_audio_context(context_id)

    async def _send_text(self, text, context_id):
        if self._context_live(context_id):
            await super()._send_text(text, context_id)

    async def run_tts(self, text, context_id):
        if not self._context_live(context_id):
            return
        generator = super().run_tts(text, context_id)
        try:
            async for frame in generator:
                if not self._context_live(context_id):
                    return
                yield frame
                # The caller may have awaited downstream work while the
                # inherited generator was suspended before context-init send.
                if not self._context_live(context_id):
                    return
        finally:
            await generator.aclose()

    async def flush_audio(self, context_id=None):
        context_id = context_id or self.get_active_audio_context_id()
        if self._context_live(context_id):
            await super().flush_audio(context_id)

    async def _handle_audio_context(self, context_id):
        token = self._origin.set(self._capture_contexts.get(context_id))
        try:
            await super()._handle_audio_context(context_id)
        finally:
            self._origin.reset(token)

    async def push_frame(self, frame, direction=FrameDirection.DOWNSTREAM):
        turn = frame_turn(frame) or self._capture_contexts.get(getattr(frame, "context_id", None)) or self._origin.get()
        if isinstance(frame, OWNED_FRAMES):
            if not live(turn):
                return
            turn.stamp(frame)
        token = self._origin.set(turn)
        try:
            await super().push_frame(frame, direction)
        finally:
            self._origin.reset(token)

    async def on_audio_context_completed(self, context_id):
        try:
            await super().on_audio_context_completed(context_id)
        finally:
            self._capture_contexts.pop(context_id, None)
            self._capture_alignment.pop(context_id, None)

    async def on_audio_context_interrupted(self, context_id):
        try:
            await super().on_audio_context_interrupted(context_id)
        finally:
            self._capture_contexts.pop(context_id, None)
            self._capture_alignment.pop(context_id, None)

    async def on_turn_context_completed(self):
        context_id = self._turn_context_id
        await super().on_turn_context_completed()
        if not self.audio_context_available(context_id):
            self._capture_contexts.pop(context_id, None)
            self._capture_alignment.pop(context_id, None)
