"""Pipecat foreground pipeline bound to the durable control plane."""

from __future__ import annotations

import asyncio
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
    UserStartedSpeakingFrame,
    UserStoppedSpeakingFrame,
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
from .fake_worker import FakeAppBuilder
from .metrics import LatencyTrace


AudioSink = Callable[[bytes, int], int]
AsyncCallback = Callable[[], Awaitable[None]]
ActionInvoker = Callable[[str, dict[str, Any], str], Awaitable[dict[str, Any]]]
StateSink = Callable[
    [str, dict[str, object], dict[str, str]], Awaitable[None]
]
TranscriptCallback = Callable[[str, bool], Awaitable[None]]


SYSTEM_INSTRUCTION = """You are Doodad, the fast foreground voice companion on a watch.
Keep spoken replies brief and natural. You may operate only through the supplied typed
tools. Never pretend a mutation succeeded before its tool result. Start durable jobs and
keep conversing; do not wait for them. A system policy, not you, schedules background
questions and completions. The current selected entity is resolved by deterministic host
code. Do not expose IDs unless the user asks."""


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


class FocusRouter(FrameProcessor):
    def __init__(
        self,
        controller: ForegroundController,
        trace: LatencyTrace,
        on_transcript: TranscriptCallback,
    ) -> None:
        super().__init__()
        self.controller = controller
        self.trace = trace
        self.on_transcript = on_transcript

    async def process_frame(self, frame: Any, direction: FrameDirection) -> None:
        await super().process_frame(frame, direction)
        # OpenAIRealtimeSTTService emits this concrete frame only for the
        # completed transcription event, but Pipecat 1.7.0 leaves the generic
        # `finalized` dataclass field at its False default. The frame type is
        # therefore the authoritative finality signal for this pinned stack.
        if isinstance(frame, InterimTranscriptionFrame):
            await self.on_transcript(frame.text, False)
        elif isinstance(frame, TranscriptionFrame):
            self.trace.mark("stt.final", characters=len(frame.text))
            await self.on_transcript(frame.text, True)
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
    def __init__(
        self,
        audio_sink: AudioSink,
        trace: LatencyTrace,
        on_user_started: AsyncCallback,
        on_user_stopped: AsyncCallback,
        on_tts_started: AsyncCallback,
        on_tts_stopped: AsyncCallback,
    ) -> None:
        super().__init__()
        self.audio_sink = audio_sink
        self.trace = trace
        self.on_user_started = on_user_started
        self.on_user_stopped = on_user_stopped
        self.on_tts_started = on_tts_started
        self.on_tts_stopped = on_tts_stopped
        self._first_tts_audio = True
        self._bot_speaking = False

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
            self._first_tts_audio = True
            await self.on_tts_started()
        elif isinstance(frame, TTSAudioRawFrame):
            if self._first_tts_audio:
                self.trace.mark("tts.first_audio")
                self._first_tts_audio = False
                await self._broadcast_bot_started()
            self.audio_sink(frame.audio, frame.sample_rate)
        elif isinstance(frame, TTSStoppedFrame):
            self.trace.mark("tts.stopped")
            await self._broadcast_bot_stopped()
            await self.on_tts_stopped()
        await self.push_frame(frame, direction)


class LiveConversation:
    """Owns one long-running Pipecat pipeline; transports may reconnect."""

    def __init__(
        self,
        controller: ForegroundController,
        builder: FakeAppBuilder,
        attention: AttentionBroker,
        trace: LatencyTrace,
        audio_sink: AudioSink,
        stop_capture: AsyncCallback,
        begin_downlink: AsyncCallback,
        end_downlink: AsyncCallback,
        wait_for_playback: AsyncCallback,
        action_invoker: ActionInvoker,
        state_sink: StateSink,
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

    async def start(self) -> None:
        missing = [
            name for name in ("OPENAI_API_KEY", "ELEVENLABS_API_KEY", "ELEVENLABS_DEFAULT_VOICE_ID")
            if not os.getenv(name)
        ]
        if missing:
            raise RuntimeError("missing provider configuration: " + ", ".join(missing))

        tools = self._tools()
        context = LLMContext(messages=[], tools=tools)
        vad = VADProcessor(
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
        llm = OpenAIResponsesLLMService(
            api_key=os.environ["OPENAI_API_KEY"],
            settings=OpenAIResponsesLLMService.Settings(
                model=os.getenv("OPENAI_FOREGROUND_MODEL", "gpt-5.6-luna"),
                system_instruction=SYSTEM_INSTRUCTION,
                max_completion_tokens=160,
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
            self.controller, self.trace, self._on_transcript,
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
            self._at_natural_pause,
        )
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
                aggregators.assistant(),
                sink,
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
        if self.worker is not None:
            await self.worker.queue_frame(InputAudioRawFrame(pcm, 16_000, 1))

    async def begin_listening(self) -> None:
        self._cancel_transcript_watchdog()
        self.user_text = ""
        self.assistant_text = ""
        await self._set_voice_phase("listening")

    async def ready(self) -> None:
        self._cancel_transcript_watchdog()
        await self._set_voice_phase("ready")

    async def cancel(self) -> None:
        self._cancel_transcript_watchdog()
        if self.worker is not None:
            await self.worker.queue_frame(InterruptionFrame())
        self.user_text = ""
        self.assistant_text = ""
        await self._set_voice_phase("ready")

    def disconnected(self) -> None:
        self._cancel_transcript_watchdog()
        self.voice_phase = "idle"

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

    async def _append_assistant_text(self, text: str) -> None:
        self.assistant_text = (self.assistant_text + text)[:160]

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

    async def close(self) -> None:
        self._cancel_transcript_watchdog()
        if self._control_task is not None:
            self._control_task.cancel()
            await asyncio.gather(self._control_task, return_exceptions=True)
        if self.worker is not None:
            await self.worker.queue_frame(EndFrame())
        if self._runner_task is not None:
            try:
                await asyncio.wait_for(self._runner_task, 10)
            except TimeoutError:
                self._runner_task.cancel()
                await asyncio.gather(self._runner_task, return_exceptions=True)
        if self._runner is not None:
            try:
                await asyncio.wait_for(self._runner.cleanup(), 5)
            except TimeoutError:
                self.trace.mark("pipeline.cleanup_timeout")

    async def _at_natural_pause(self) -> None:
        # Flush the utterance's stateful resampler before deciding whether to
        # enqueue another announcement or wait for the physical playout tail.
        await self.end_downlink()
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
            await self.wait_for_playback()
            await self._set_voice_phase("ready")

    async def _control_loop(self) -> None:
        while True:
            now_ms = int(time.time() * 1000)
            self.builder.tick(now_ms)
            rows = self.attention.store.connection.execute(
                "SELECT job_id FROM jobs ORDER BY created_at_ms,job_id"
            ).fetchall()
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
            "response": self.assistant_text,
        }

    async def _publish_state(self) -> None:
        await self.state_sink(
            self.voice_phase,
            self.attention.background_snapshot(),
            self._display_state(),
        )

    def _tools(self) -> list[FunctionSchema]:
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

        async def next_set(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="get_next_set")
            result = await self.action_invoker(
                "get_next_set", {}, params.tool_call_id
            )
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="get_next_set")

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

        async def build(params: FunctionCallParams) -> None:
            self.trace.mark("tool.start", tool="start_app_build")
            result = self.controller.start_app_build(str(params.arguments["brief"]))
            await params.result_callback(result)
            self.trace.mark("tool.end", tool="start_app_build", job_id=result["job_id"])

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
                "Start a durable fake app build and return immediately.",
                {"brief": {"type": "string"}},
                ["brief"],
                handler=build,
            ),
        ]
