"""Pipecat foreground pipeline bound to the durable control plane."""

from __future__ import annotations

import asyncio
import copy
import functools
import os
import time
import uuid
from collections.abc import Awaitable, Callable
from typing import Any

from pipecat.adapters.schemas.function_schema import FunctionSchema
from pipecat.audio.vad.silero import SileroVADAnalyzer
from pipecat.audio.vad.vad_analyzer import VADParams
from pipecat.frames.frames import (
    BotStartedSpeakingFrame,
    BotStoppedSpeakingFrame,
    EndFrame,
    InputAudioRawFrame,
    InterruptionFrame,
    InterimTranscriptionFrame,
    LLMFullResponseStartFrame,
    LLMTextFrame,
    StartFrame,
    TranscriptionFrame,
    TTSAudioRawFrame,
    TTSSpeakFrame,
    TTSStartedFrame,
    TTSStoppedFrame,
    TTSTextFrame,
    UserStartedSpeakingFrame,
    UserStoppedSpeakingFrame,
    VADUserStartedSpeakingFrame,
    VADUserStoppedSpeakingFrame,
)
from pipecat.pipeline.pipeline import Pipeline
from pipecat.pipeline.worker import PipelineParams, PipelineWorker
from pipecat.processors.audio.vad_processor import VADProcessor
from pipecat.processors.aggregators.llm_context import LLMContext
from pipecat.processors.aggregators.llm_response_universal import (
    LLMContextAggregatorPair,
    LLMUserAggregatorParams,
)
from pipecat.processors.frame_processor import FrameDirection, FrameProcessor
from pipecat.services.elevenlabs.tts import ElevenLabsTTSService
from pipecat.services.llm_service import FunctionCallParams
from pipecat.services.openai.responses.llm import (
    OpenAIResponsesLLMService,
    OpenAIResponsesReasoningConfig,
)
from pipecat.services.openai.stt import OpenAIRealtimeSTTService
from pipecat.workers.runner import WorkerRunner

from .attention import AttentionBroker
from .controller import ForegroundController
from .builder import AppBuilder
from .metrics import LatencyTrace


AudioSink = Callable[[bytes, int], int]
AsyncCallback = Callable[[], Awaitable[None]]
ActionInvoker = Callable[[str, dict[str, Any], str], Awaitable[dict[str, Any]]]
StateSink = Callable[
    [str, dict[str, object], dict[str, str]], Awaitable[None]
]
TranscriptCallback = Callable[[str, bool], Awaitable[None]]


def _positive_env_int(name: str, default: int) -> int:
    value = int(os.getenv(name, str(default)))
    if value <= 0:
        raise ValueError(f"{name} must be positive")
    return value


SYSTEM_INSTRUCTION = """You are Doodad, the fast foreground voice companion on a watch.
Default to brief, natural spoken replies, but give a complete longer answer when the user
requests one or the subject genuinely requires it. You may operate only through the supplied typed
tools. Never pretend a mutation succeeded before its tool result. Start durable jobs and
keep conversing; do not wait for them. A system policy, not you, schedules background
questions and completions. The current selected entity is resolved by deterministic host
code. Use get_task_status whenever the user asks what an agent or background task is
doing; do not rely on conversational memory for task state. Research reports and
presentation-delivery requests are durable general background work, not app builds.
Do not expose IDs unless the user asks."""


class TracedSileroVADAnalyzer(SileroVADAnalyzer):
    def __init__(self, trace: LatencyTrace, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        self._trace = trace
        self._confidence_windows = 0

    def voice_confidence(self, buffer: bytes) -> float:
        raw_confidence = super().voice_confidence(buffer)
        confidence = float(
            raw_confidence.item()
            if hasattr(raw_confidence, "item")
            else raw_confidence
        )
        self._confidence_windows += 1
        if self._confidence_windows % 50 == 0 or (
            confidence >= 0.5 and self._confidence_windows % 10 == 0
        ):
            self._trace.mark(
                "vad.confidence",
                confidence=round(confidence, 4),
                windows=self._confidence_windows,
            )
        return confidence


class PipelineProbe(FrameProcessor):
    def __init__(self, trace: LatencyTrace, stage: str, count_audio: bool = False) -> None:
        super().__init__()
        self.trace = trace
        self.stage = stage
        self.count_audio = count_audio
        self.frames = 0

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        if isinstance(frame, StartFrame):
            self.trace.mark("pipeline.start_reached", stage=self.stage)
        if self.count_audio and isinstance(frame, InputAudioRawFrame):
            self.frames += 1
            if self.frames % 100 == 0:
                self.trace.mark("pipeline.audio_frames", frames=self.frames)
        await self.push_frame(frame, direction)


class CaptureBoundaryProcessor(FrameProcessor):
    """Clear abandoned STT input before an explicitly authorized capture.

    Start/audio/end are SystemFrames, so the pinned processor queues preserve
    their order. No silence detector can commit a partial PTT recording.
    """

    def __init__(self, clear_audio: AsyncCallback) -> None:
        super().__init__()
        self.clear_audio = clear_audio

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        if direction == FrameDirection.DOWNSTREAM and isinstance(frame, VADUserStartedSpeakingFrame):
            await self.clear_audio()
        await self.push_frame(frame, direction)


class FocusRouter(FrameProcessor):
    def __init__(
        self,
        controller: ForegroundController,
        trace: LatencyTrace,
        on_transcript: TranscriptCallback,
        is_current: Callable[[], bool] = lambda: True,
    ) -> None:
        super().__init__()
        self.controller = controller
        self.trace = trace
        self.on_transcript = on_transcript
        self.is_current = is_current

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        if isinstance(frame, (InterimTranscriptionFrame, TranscriptionFrame)) and not self.is_current():
            return
        # OpenAIRealtimeSTTService emits this concrete frame only for the
        # completed transcription event, but Pipecat 1.7.0 leaves the generic
        # `finalized` dataclass field at its False default. The frame type is
        # therefore the authoritative finality signal for this pinned stack.
        if isinstance(frame, InterimTranscriptionFrame):
            await self.on_transcript(frame.text, False)
        elif isinstance(frame, TranscriptionFrame):
            self.trace.mark("stt.final", characters=len(frame.text))
            await self.on_transcript(frame.text, True)
            if not self.is_current():
                return
            utterance_id = f"utt_{uuid.uuid4().hex}"
            routed = self.controller.route_focused(frame.text, utterance_id)
            if routed.handled:
                self.trace.mark(
                    "focus.answer",
                    job_id=routed.job_id,
                    question_id=routed.question_id,
                    answer=routed.answer,
                )
                # The broker has already validated and durably applied this
                # typed answer. Do not phrase it as a new user request and let
                # the foreground model infer again; that can accidentally
                # invoke an unrelated tool. Consume the transcript and send a
                # deterministic confirmation straight to TTS instead.
                await self.push_frame(
                    TTSSpeakFrame(f"Got it—{routed.answer} selected for that build."),
                    direction,
                )
                return
        await self.push_frame(frame, direction)


class AssistantTextTap(FrameProcessor):
    """Collect bounded assistant text without changing the TTS frame stream."""

    def __init__(
        self,
        on_reset: AsyncCallback,
        on_text: Callable[[str], Awaitable[None]],
    ) -> None:
        super().__init__()
        self.on_reset = on_reset
        self.on_text = on_text

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        if isinstance(frame, LLMFullResponseStartFrame):
            await self.on_reset()
        elif isinstance(frame, LLMTextFrame):
            await self.on_text(frame.text)
        elif isinstance(frame, TTSSpeakFrame):
            await self.on_reset()
            await self.on_text(frame.text)
        await self.push_frame(frame, direction)


class ConversationSink(FrameProcessor):
    """Bridge TTS to the watch while preserving playout semantics.

    Pipecat's ElevenLabs service emits word-aligned ``TTSTextFrame`` objects as
    soon as synthesis produces them. Our custom watch transport must not commit
    those words to conversation history before the corresponding utterance has
    actually played. We therefore retain them until playout drains and discard
    them if a high-priority interruption overtakes the utterance.
    """

    def __init__(
        self,
        audio_sink: AudioSink,
        trace: LatencyTrace,
        on_user_started: AsyncCallback,
        on_user_stopped: AsyncCallback,
        on_tts_started: AsyncCallback,
        on_playout_drain: AsyncCallback,
        on_tts_stopped: AsyncCallback,
    ) -> None:
        super().__init__()
        self.audio_sink = audio_sink
        self.trace = trace
        self.on_user_started = on_user_started
        self.on_user_stopped = on_user_stopped
        self.on_tts_started = on_tts_started
        self.on_playout_drain = on_playout_drain
        self.on_tts_stopped = on_tts_stopped
        self._first_tts_audio = True
        self._bot_speaking = False
        self._utterance_generation = 0
        self._utterance_interrupted = False
        self._pending_tts_text: list[tuple[TTSTextFrame, FrameDirection]] = []
        self._retired = False
        self._tts_active = False
        self._tts_context: str | None = None

    def retire(self) -> None:
        """Synchronous fence, before queued provider cancellation can run."""
        self._retired = True
        self._utterance_generation += 1
        self._tts_active = False
        self._pending_tts_text.clear()

    async def _broadcast_bot_started(self) -> None:
        if self._bot_speaking:
            return
        self._bot_speaking = True
        downstream = BotStartedSpeakingFrame()
        upstream = BotStartedSpeakingFrame()
        upstream.broadcast_sibling_id = downstream.id
        downstream.broadcast_sibling_id = upstream.id
        await self.push_frame(downstream)
        await self.push_frame(upstream, FrameDirection.UPSTREAM)

    async def _broadcast_bot_stopped(self) -> None:
        if not self._bot_speaking:
            return
        self._bot_speaking = False
        downstream = BotStoppedSpeakingFrame()
        upstream = BotStoppedSpeakingFrame()
        upstream.broadcast_sibling_id = downstream.id
        downstream.broadcast_sibling_id = upstream.id
        await self.push_frame(downstream)
        await self.push_frame(upstream, FrameDirection.UPSTREAM)

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        if self._retired and isinstance(frame, (TTSStartedFrame, TTSAudioRawFrame, TTSTextFrame,
                                                TTSStoppedFrame, LLMFullResponseStartFrame,
                                                UserStartedSpeakingFrame, UserStoppedSpeakingFrame)):
            return
        if isinstance(frame, InterruptionFrame):
            self._utterance_interrupted = True
            self._tts_active = False
            self._pending_tts_text.clear()
            await self.push_frame(frame, direction)
            await self._broadcast_bot_stopped()
            return
        if isinstance(frame, UserStartedSpeakingFrame):
            self.trace.mark("vad.user_started")
            await self.on_user_started()
        elif isinstance(frame, UserStoppedSpeakingFrame):
            self.trace.mark("vad.user_stopped")
            await self.on_user_stopped()
        elif isinstance(frame, LLMFullResponseStartFrame):
            self.trace.mark("llm.first_response")
        elif isinstance(frame, TTSStartedFrame):
            self.trace.mark("tts.started")
            self._utterance_generation += 1
            self._utterance_interrupted = False
            self._pending_tts_text.clear()
            self._first_tts_audio = True
            self._tts_active = True
            self._tts_context = frame.context_id
            await self.on_tts_started()
        elif isinstance(frame, TTSAudioRawFrame):
            if not self._tts_active or frame.context_id != self._tts_context:
                return
            if self._first_tts_audio:
                self.trace.mark("tts.first_audio")
                self._first_tts_audio = False
                await self._broadcast_bot_started()
            self.audio_sink(frame.audio, frame.sample_rate)
        elif isinstance(frame, TTSTextFrame):
            if not self._tts_active or frame.context_id != self._tts_context:
                return
            self._pending_tts_text.append((frame, direction))
            return
        elif isinstance(frame, TTSStoppedFrame):
            if not self._tts_active or frame.context_id != self._tts_context:
                return
            self.trace.mark("tts.stopped")
            generation = self._utterance_generation
            await self.on_playout_drain()
            if self._utterance_interrupted or generation != self._utterance_generation:
                if generation == self._utterance_generation:
                    self._pending_tts_text.clear()
                return
            self._tts_active = False
            pending_text = self._pending_tts_text
            self._pending_tts_text = []
            for text_frame, text_direction in pending_text:
                await self.push_frame(text_frame, text_direction)
            await self.push_frame(frame, direction)
            await self._broadcast_bot_stopped()
            await self.on_tts_stopped()
            return
        await self.push_frame(frame, direction)


class LiveConversation:
    """Owns one long-running Pipecat pipeline; transports may reconnect."""

    def __init__(
        self,
        controller: ForegroundController,
        builder: AppBuilder,
        attention: AttentionBroker,
        trace: LatencyTrace,
        audio_sink: AudioSink,
        stop_capture: AsyncCallback,
        begin_downlink: AsyncCallback,
        end_downlink: AsyncCallback,
        wait_for_playback: AsyncCallback,
        action_invoker: ActionInvoker,
        state_sink: StateSink,
        *, history: list | None = None, explicit_capture: bool = False,
    ) -> None:
        self.controller = controller
        self.builder = builder
        self.attention = attention
        self.trace = trace
        self.audio_sink = audio_sink
        self.stop_capture = stop_capture
        self.begin_downlink = begin_downlink
        self.end_downlink = end_downlink
        self.wait_for_playback = wait_for_playback
        self.action_invoker = action_invoker
        self.state_sink = state_sink
        self.voice_phase = "idle"
        self.worker: PipelineWorker | None = None
        self._runner: WorkerRunner | None = None
        self._runner_task: asyncio.Task[None] | None = None
        self._control_task: asyncio.Task[None] | None = None
        self._transcript_watchdog_task: asyncio.Task[None] | None = None
        self._current_turn_has_final = False
        self._seen_events: set[str] = set()
        self.user_text = ""
        self.assistant_text = ""
        self._max_response_text_bytes = _positive_env_int(
            "DOODAD_MAX_RESPONSE_TEXT_BYTES", 262_144
        )
        self._response_journal_full = False
        self._retired = False
        self._sink: ConversationSink | None = None
        self._context: LLMContext | None = None
        self._history = copy.deepcopy(history or [])
        self._retirement_task: asyncio.Task | None = None
        self.explicit_capture = explicit_capture
        self._capture_open = False

    async def start(self) -> None:
        missing = [
            name for name in ("OPENAI_API_KEY", "ELEVENLABS_API_KEY", "ELEVENLABS_DEFAULT_VOICE_ID")
            if not os.getenv(name)
        ]
        if missing:
            raise RuntimeError("missing provider configuration: " + ", ".join(missing))

        tools = self._tools()
        context = LLMContext(messages=self._history, tools=tools)
        self._context = context
        vad = None if self.explicit_capture else VADProcessor(
            vad_analyzer=TracedSileroVADAnalyzer(
                self.trace,
                sample_rate=16_000,
                params=VADParams(
                    # CoreS3 speech is consistently separated from room noise
                    # (about 0.08 versus 0.01). Calibrate to the physical
                    # microphone path instead of Silero's generic 0.5 default.
                    confidence=0.05,
                    start_secs=0.15,
                    stop_secs=0.2,
                    min_volume=0.03,
                ),
            )
        )
        aggregators = LLMContextAggregatorPair(
            context,
            user_params=LLMUserAggregatorParams(
                user_turn_stop_timeout=3.0,
            ),
        )
        stt = OpenAIRealtimeSTTService(
            api_key=os.environ["OPENAI_API_KEY"],
            settings=OpenAIRealtimeSTTService.Settings(
                model=os.getenv("OPENAI_STT_MODEL", "gpt-realtime-whisper"),
                noise_reduction="near_field",
            ),
            turn_detection=False,
        )
        if self.explicit_capture:
            vad = CaptureBoundaryProcessor(stt._clear_audio_buffer)
        llm = OpenAIResponsesLLMService(
            api_key=os.environ["OPENAI_API_KEY"],
            settings=OpenAIResponsesLLMService.Settings(
                model=os.getenv("OPENAI_FOREGROUND_MODEL", "gpt-5.6-luna"),
                system_instruction=SYSTEM_INSTRUCTION,
                max_completion_tokens=_positive_env_int(
                    "DOODAD_MAX_COMPLETION_TOKENS", 4096
                ),
                reasoning=OpenAIResponsesReasoningConfig(effort="none"),
            ),
        )
        tts = ElevenLabsTTSService(
            api_key=os.environ["ELEVENLABS_API_KEY"],
            settings=ElevenLabsTTSService.Settings(
                voice=os.environ["ELEVENLABS_DEFAULT_VOICE_ID"],
                model=os.getenv("DOODAD_ELEVENLABS_MODEL_ID", "eleven_flash_v2_5"),
            ),
            sample_rate=16_000,
            auto_mode=True,
        )

        router = FocusRouter(
            self.controller, self.trace, self._on_transcript, lambda: not self._retired,
        )
        assistant_text = AssistantTextTap(
            self._clear_assistant_text,
            self._append_assistant_text,
        )
        sink = ConversationSink(
            self.audio_sink,
            self.trace,
            self._begin_user_turn,
            self._finish_user_turn,
            self._begin_speaking,
            self._drain_downlink,
            self._at_natural_pause,
        )
        self._sink = sink
        pipeline = Pipeline(
            [
                PipelineProbe(self.trace, "ingress", count_audio=True),
                vad,
                stt,
                PipelineProbe(self.trace, "after_stt"),
                router,
                aggregators.user(),
                llm,
                PipelineProbe(self.trace, "after_llm"),
                assistant_text,
                tts,
                PipelineProbe(self.trace, "after_tts"),
                sink,
                aggregators.assistant(),
            ]
        )
        self.worker = PipelineWorker(
            pipeline,
            enable_rtvi=False,
            cancel_on_idle_timeout=False,
            idle_timeout_secs=None,
            params=PipelineParams(
                audio_in_sample_rate=16_000,
                audio_out_sample_rate=16_000,
                enable_metrics=True,
                report_only_initial_ttfb=False,
            ),
        )
        self._runner = WorkerRunner(handle_sigint=False)
        await self._runner.add_workers(self.worker)
        self._runner_task = asyncio.create_task(self._runner.run())
        self._control_task = asyncio.create_task(self._control_loop())
        await self._set_voice_phase("idle")

    async def feed_audio(self, pcm: bytes) -> None:
        if self.explicit_capture and not self._capture_open:
            return
        if self.worker is not None and not self._retired:
            await self.worker.queue_frame(InputAudioRawFrame(pcm, 16_000, 1))

    async def capture_started(self) -> None:
        if self.explicit_capture and not self._retired and self.worker is not None and not self._capture_open:
            self._capture_open = True
            await self.worker.queue_frame(VADUserStartedSpeakingFrame(start_secs=0.0))

    async def capture_completed(self) -> None:
        if self.explicit_capture and self._capture_open and not self._retired and self.worker is not None:
            self._capture_open = False
            await self.worker.queue_frame(VADUserStoppedSpeakingFrame(stop_secs=0.0))

    async def submit_text(self, text: str) -> None:
        """Inject one final user turn after capture, bypassing only the microphone/STT."""

        bounded = " ".join(text.split())[:500]
        if not bounded or self.worker is None:
            return
        await self.begin_listening()
        await self.worker.queue_frame(
            TranscriptionFrame(
                text=bounded,
                user_id="watch-text",
                timestamp="",
                language="en",
                finalized=False,
            )
        )

    async def begin_listening(self) -> None:
        self._cancel_transcript_watchdog()
        self.user_text = ""
        self.assistant_text = ""
        await self._set_voice_phase("listening")

    async def ready(self) -> None:
        self._cancel_transcript_watchdog()
        await self._set_voice_phase("ready")

    async def cancel(self) -> None:
        self._capture_open = False
        self._cancel_transcript_watchdog()
        if self.worker is not None:
            await self.worker.queue_frame(InterruptionFrame())
        self.user_text = ""
        self.assistant_text = ""
        await self._set_voice_phase("ready")

    def disconnected(self) -> None:
        self._capture_open = False
        self._cancel_transcript_watchdog()
        self.voice_phase = "idle"
        self._retired = True
        if self._sink is not None:
            self._sink.retire()
        if self._retirement_task is None:
            self._retirement_task = asyncio.create_task(self._stop_pipeline())

    def history(self) -> list:
        return copy.deepcopy(self._context.get_messages() if self._context else self._history)

    async def _begin_user_turn(self) -> None:
        self._current_turn_has_final = False
        self._cancel_transcript_watchdog()

    async def _finish_user_turn(self) -> None:
        await self.stop_capture()
        await self._set_voice_phase("thinking")
        self._cancel_transcript_watchdog()
        if not self._current_turn_has_final:
            self._transcript_watchdog_task = asyncio.create_task(
                self._recover_missing_transcript()
            )

    async def _on_transcript(self, text: str, final: bool) -> None:
        self.user_text = text[:160]
        if final:
            self._current_turn_has_final = True
            self._cancel_transcript_watchdog()
            await self.stop_capture()
            await self._set_voice_phase("thinking")
        elif self.voice_phase == "listening":
            await self._publish_state()

    async def _clear_assistant_text(self) -> None:
        self.assistant_text = ""
        self._response_journal_full = False

    async def _append_assistant_text(self, text: str) -> None:
        current_bytes = len(self.assistant_text.encode("utf-8"))
        remaining = self._max_response_text_bytes - current_bytes
        if remaining <= 0:
            if not self._response_journal_full:
                self.trace.mark(
                    "response.journal_capacity_exceeded",
                    capacity_bytes=self._max_response_text_bytes,
                )
                self._response_journal_full = True
            return
        encoded = text.encode("utf-8")
        addition = encoded[:remaining].decode("utf-8", errors="ignore")
        self.assistant_text += addition
        if len(encoded) > remaining and not self._response_journal_full:
            self.trace.mark(
                "response.journal_capacity_exceeded",
                capacity_bytes=self._max_response_text_bytes,
            )
            self._response_journal_full = True

    async def _begin_speaking(self) -> None:
        await self.stop_capture()
        await self.begin_downlink()
        await self._set_voice_phase("speaking")

    def _cancel_transcript_watchdog(self) -> None:
        if self._transcript_watchdog_task is not None:
            self._transcript_watchdog_task.cancel()
            self._transcript_watchdog_task = None

    async def _recover_missing_transcript(self) -> None:
        task = asyncio.current_task()
        try:
            await asyncio.sleep(4)
            if self.voice_phase == "thinking":
                self.trace.mark("stt.missing_final")
                await self._set_voice_phase("ready")
        finally:
            if self._transcript_watchdog_task is task:
                self._transcript_watchdog_task = None

    async def interrupt(self) -> None:
        if self.worker is not None:
            await self.worker.queue_frame(InterruptionFrame())

    async def close(self, *, close_builder: bool = True) -> None:
        self.disconnected()
        self._cancel_transcript_watchdog()
        if self._control_task is not None:
            self._control_task.cancel()
            await asyncio.gather(self._control_task, return_exceptions=True)
        if close_builder:
            self.builder.close()
        if self._retirement_task is not None:
            await self._retirement_task

    async def _stop_pipeline(self) -> None:
        if self.worker is not None:
            await self.worker.cancel(reason='watch provider session retired')
        if self._runner_task is not None:
            try:
                await asyncio.wait_for(self._runner_task, 4)
            except TimeoutError:
                self._runner_task.cancel()
                await asyncio.gather(self._runner_task, return_exceptions=True)
        if self._runner is not None:
            try:
                await asyncio.wait_for(self._runner.cleanup(), 2)
            except TimeoutError:
                self.trace.mark("pipeline.cleanup_timeout")

    async def _drain_downlink(self) -> None:
        # Flush the stateful resampler, then let the selected transport wait
        # for playout (MoQ requires the matching watch receipt). There is no fixed
        # response-length timeout here; interruption clears the generation and
        # releases this wait.
        await self.end_downlink()
        await self.wait_for_playback()

    async def _at_natural_pause(self) -> None:
        if self.voice_phase != "speaking":
            self.trace.mark(
                "downlink.stale_stop", voice_phase=self.voice_phase
            )
            return
        action = self.attention.natural_pause(int(time.time() * 1000))
        if action is not None and self.worker is not None:
            self.trace.mark(
                "attention.spoken", kind_detail=action.kind, job_id=action.job_id
            )
            await self.worker.queue_frame(TTSSpeakFrame(action.text))
        else:
            await self._set_voice_phase("ready")

    async def _control_loop(self) -> None:
        while True:
            now_ms = int(time.time() * 1000)
            self.builder.tick(now_ms)
            rows = self.attention.store.fetch_all(
                "SELECT job_id FROM jobs WHERE device_id=? "
                "ORDER BY created_at_ms,job_id",
                (self.attention.device_id,),
            )
            for row in rows:
                for event in self.attention.jobs.events(row["job_id"]):
                    if event.event_id not in self._seen_events:
                        self.attention.observe(event, now_ms)
                        self._seen_events.add(event.event_id)
                        self.trace.mark(
                            "job.event", job_id=event.job_id, event_kind=event.kind
                        )
            await self.state_sink(
                self.voice_phase,
                self.attention.background_snapshot(),
                self._display_state(),
            )
            await asyncio.sleep(0.1)

    async def _set_voice_phase(self, phase: str) -> None:
        self.voice_phase = phase
        await self._publish_state()

    def _display_state(self) -> dict[str, str]:
        return {
            "transcript": self.user_text,
            # The server retains the complete response journal. The square
            # watch gets only a compact live projection of its tail.
            "response": self.assistant_text[-160:],
        }

    async def _publish_state(self) -> None:
        if getattr(self, '_retired', False):
            return
        await self.state_sink(
            self.voice_phase,
            self.attention.background_snapshot(),
            self._display_state(),
        )

    def _current_tool(self, handler):
        @functools.wraps(handler)
        async def guarded(params):
            if self._retired:
                raise ConnectionError('provider session retired')
            return await handler(params)
        return guarded

    def _tools(self) -> list[FunctionSchema]:
        @self._current_tool
        async def record(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="record_missed_set")
            state = self.controller.kernel.snapshot()
            result = await self.action_invoker(
                "record_missed_set",
                {
                    "workout_id": state["active_workout_id"],
                    "set_id": state["selected_entity"],
                    "expected_revision": state["revision"],
                },
                params.tool_call_id,
            )
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="record_missed_set")

        @self._current_tool
        async def next_set(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="get_next_set")
            result = await self.action_invoker(
                "get_next_set", {}, params.tool_call_id
            )
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="get_next_set")

        @self._current_tool
        async def food(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="log_food")
            arguments = params.arguments
            result = await self.action_invoker(
                "log_food",
                {
                    "description": str(arguments["description"]),
                    "quantity": float(arguments.get("quantity", 1)),
                    "unit": str(arguments.get("unit", "item")),
                },
                params.tool_call_id,
            )
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="log_food")

        @self._current_tool
        async def build(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="start_app_build")
            result = self.controller.start_app_build(str(params.arguments["brief"]))
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="start_app_build", job_id=result["job_id"])

        @self._current_tool
        async def background_work(params: FunctionCallParams) -> None:
            arguments = params.arguments
            kind = str(arguments["kind"])
            self.trace.mark("tool.start", tool="start_background_work", work_kind=kind)
            result = self.controller.start_background_work(
                kind,
                str(arguments["brief"]),
                recipient=(
                    str(arguments["recipient"])
                    if arguments.get("recipient") else None
                ),
            )
            await params.result_callback(result)
            self.trace.mark(
                "tool.end", tool="start_background_work", job_id=result["job_id"]
            )

        @self._current_tool
        async def task_status(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="get_task_status")
            result = self.controller.task_status(
                str(params.arguments.get("query", "")) or None
            )
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="get_task_status", count=result["count"])

        return [
            FunctionSchema(
                "record_missed_set",
                "Mark the currently selected set missed using the synchronized revision.",
                {},
                [],
                handler=record,
            ),
            FunctionSchema(
                "get_next_set", "Read the next pending set after the selected set.", {}, [], handler=next_set
            ),
            FunctionSchema(
                "log_food",
                "Immediately record a provisional food fact.",
                {
                    "description": {"type": "string"},
                    "quantity": {"type": "number", "default": 1},
                    "unit": {"type": "string", "default": "item"},
                },
                ["description"],
                handler=food,
            ),
            FunctionSchema(
                "start_app_build",
                "Start a durable app build and return immediately.",
                {"brief": {"type": "string"}},
                ["brief"],
                handler=build,
            ),
            FunctionSchema(
                "start_background_work",
                "Start durable research or presentation work and return immediately.",
                {
                    "kind": {
                        "type": "string",
                        "enum": ["research_report", "presentation_delivery"],
                    },
                    "brief": {"type": "string"},
                    "recipient": {"type": "string"},
                },
                ["kind", "brief"],
                handler=background_work,
            ),
            FunctionSchema(
                "get_task_status",
                "Read durable current status for all tasks or a named matching task.",
                {"query": {"type": "string"}},
                [],
                handler=task_status,
            ),
        ]
