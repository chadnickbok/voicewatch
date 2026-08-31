"""Carry admitted capture ownership through the persistent provider pipeline.

Owners come from stamped input/context frames or a registered TTS context,
never from whichever capture happens to be current when a callback arrives.
These adapters extend the pinned Pipecat services without editing dependencies.

Work in progress: these adapters are not wired into LiveConversation yet.
Import and aggregator construction have been smoke-checked; downstream
cancellation behavior still needs integration and dedicated validation.
"""
from __future__ import annotations

import json
from contextvars import ContextVar

from pipecat.frames.frames import (
    AggregatedTextProgressFrame, BotStartedSpeakingFrame, BotStoppedSpeakingFrame,
    FunctionCallCancelFrame, FunctionCallInProgressFrame, FunctionCallResultFrame,
    FunctionCallsStartedFrame, InputAudioRawFrame, InterimTranscriptionFrame,
    InterruptionFrame, LLMAssistantPushAggregationFrame, LLMContextFrame,
    LLMFullResponseEndFrame, LLMFullResponseStartFrame, LLMThoughtEndFrame,
    LLMThoughtStartFrame, LLMThoughtTextFrame, TextFrame, TranscriptionFrame,
    TTSAudioRawFrame, TTSSpeakFrame, TTSStartedFrame, TTSStoppedFrame,
    UserStartedSpeakingFrame, UserStoppedSpeakingFrame,
    VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame,
)
from pipecat.processors.aggregators.llm_response_universal import (
    LLMAssistantAggregator, LLMUserAggregator, LLMUserAggregatorParams,
)
from pipecat.processors.frame_processor import FrameDirection
from pipecat.services.elevenlabs.tts import ElevenLabsTTSService
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


class CaptureAggregatorPair:
    def __init__(self, context, *, user_params=None):
        self._user = CaptureUserAggregator(context, params=user_params or LLMUserAggregatorParams(),
                                          _realtime_service_mode=False)
        self._assistant = CaptureAssistantAggregator(context, _realtime_service_mode=False,
                                                     _paired_user_aggregator=self._user)

    def user(self): return self._user
    def assistant(self): return self._assistant


class CaptureResponsesLLMService(OpenAIResponsesLLMService):
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
        context_id = super().create_context_id()
        turn = self._origin.get()
        if not live(turn):
            raise RuntimeError("TTS capture retired")
        self._capture_contexts = {key: owner for key, owner in self._capture_contexts.items() if owner.live}
        if context_id not in self._capture_contexts and len(self._capture_contexts) >= self.MAX_CONTEXTS:
            raise RuntimeError("TTS capture context capacity")
        previous = self._capture_contexts.get(context_id)
        if previous is not None and previous is not turn:
            raise RuntimeError("TTS context owner changed")
        self._capture_contexts[context_id] = turn
        return context_id

    def _get_websocket(self):
        return _ContextMessages(super()._get_websocket(), self._context_live)

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

    async def on_audio_context_interrupted(self, context_id):
        try:
            await super().on_audio_context_interrupted(context_id)
        finally:
            self._capture_contexts.pop(context_id, None)

    async def on_turn_context_completed(self):
        context_id = self._turn_context_id
        await super().on_turn_context_completed()
        if not self.audio_context_available(context_id):
            self._capture_contexts.pop(context_id, None)
