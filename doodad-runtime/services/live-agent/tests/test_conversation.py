from __future__ import annotations

import asyncio
from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    BotStartedSpeakingFrame,
    BotStoppedSpeakingFrame,
    TTSAudioRawFrame,
    TTSStartedFrame,
    TTSStoppedFrame,
    TTSSpeakFrame,
    TranscriptionFrame,
)
from pipecat.processors.frame_processor import FrameDirection

from doodad_agent.conversation import ConversationSink, FocusRouter, LiveConversation
from doodad_agent.metrics import LatencyTrace
from doodad_agent.transport import DownlinkAudioTrack


class StubAttention:
    def background_snapshot(self) -> dict[str, object]:
        return {}


@pytest.mark.asyncio
async def test_final_before_user_stopped_does_not_arm_missing_transcript_watchdog() -> None:
    phases: list[str] = []

    async def state_sink(
        phase: str,
        _background: dict[str, object],
        _display: dict[str, str],
    ) -> None:
        phases.append(phase)

    async def stop_capture() -> None:
        return None

    conversation = object.__new__(LiveConversation)
    conversation.voice_phase = "listening"
    conversation.attention = StubAttention()
    conversation.state_sink = state_sink
    conversation._transcript_watchdog_task = None
    conversation._current_turn_has_final = False
    conversation.user_text = ""
    conversation.assistant_text = ""
    conversation.stop_capture = stop_capture

    await conversation._begin_user_turn()
    await conversation._on_transcript("hello", True)
    await conversation._finish_user_turn()

    assert phases == ["thinking", "thinking"]
    assert conversation._transcript_watchdog_task is None


@pytest.mark.asyncio
async def test_missing_final_still_arms_recovery_watchdog() -> None:
    async def state_sink(
        _phase: str,
        _background: dict[str, object],
        _display: dict[str, str],
    ) -> None:
        return None

    async def stop_capture() -> None:
        return None

    conversation = object.__new__(LiveConversation)
    conversation.voice_phase = "listening"
    conversation.attention = StubAttention()
    conversation.state_sink = state_sink
    conversation._transcript_watchdog_task = None
    conversation._current_turn_has_final = False
    conversation.user_text = ""
    conversation.assistant_text = ""
    conversation.stop_capture = stop_capture

    await conversation._begin_user_turn()
    await conversation._finish_user_turn()

    assert conversation._transcript_watchdog_task is not None
    conversation._cancel_transcript_watchdog()
    await asyncio.sleep(0)


@pytest.mark.asyncio
async def test_missing_final_recovery_returns_to_ready_without_capture(monkeypatch) -> None:
    order: list[str] = []

    async def no_wait(_seconds: float) -> None:
        return None

    async def state_sink(
        phase: str,
        _background: dict[str, object],
        _display: dict[str, str],
    ) -> None:
        order.append(phase)

    monkeypatch.setattr(asyncio, "sleep", no_wait)
    conversation = object.__new__(LiveConversation)
    conversation.voice_phase = "thinking"
    conversation.attention = StubAttention()
    conversation.state_sink = state_sink
    conversation.trace = LatencyTrace()
    conversation._transcript_watchdog_task = None
    conversation.user_text = ""
    conversation.assistant_text = ""

    await conversation._recover_missing_transcript()

    assert order == ["ready"]


@pytest.mark.asyncio
async def test_push_to_talk_turn_stays_off_until_explicit_activation() -> None:
    published: list[tuple[str, dict[str, str]]] = []
    stops = 0

    async def state_sink(
        phase: str,
        _background: dict[str, object],
        display: dict[str, str],
    ) -> None:
        published.append((phase, display.copy()))

    async def stop_capture() -> None:
        nonlocal stops
        stops += 1

    conversation = object.__new__(LiveConversation)
    conversation.voice_phase = "idle"
    conversation.attention = StubAttention()
    conversation.state_sink = state_sink
    conversation.stop_capture = stop_capture
    conversation._transcript_watchdog_task = None
    conversation._current_turn_has_final = False
    conversation.user_text = "old transcript"
    conversation.assistant_text = "old response"

    await conversation.ready()
    assert stops == 0
    assert published[-1] == (
        "ready",
        {"transcript": "old transcript", "response": "old response"},
    )

    await conversation.begin_listening()
    assert stops == 0
    assert published[-1] == (
        "listening",
        {"transcript": "", "response": ""},
    )

    await conversation._on_transcript("Can you hear me?", False)
    assert stops == 0
    assert published[-1] == (
        "listening",
        {"transcript": "Can you hear me?", "response": ""},
    )

    await conversation._on_transcript("Can you hear me?", True)
    assert stops == 1
    assert published[-1] == (
        "thinking",
        {"transcript": "Can you hear me?", "response": ""},
    )


@pytest.mark.asyncio
async def test_playback_completion_returns_to_ready_without_rearming_capture() -> None:
    order: list[str] = []

    async def state_sink(
        phase: str,
        _background: dict[str, object],
        _display: dict[str, str],
    ) -> None:
        order.append(phase)

    async def wait_for_playback() -> None:
        order.append("drained")

    class Attention(StubAttention):
        def natural_pause(self, _now_ms: int):
            return None

    conversation = object.__new__(LiveConversation)
    conversation.voice_phase = "speaking"
    conversation.attention = Attention()
    conversation.state_sink = state_sink
    conversation.wait_for_playback = wait_for_playback
    conversation.worker = None
    conversation.user_text = "Hello"
    conversation.assistant_text = "Hi there"

    await conversation._at_natural_pause()

    assert order == ["drained", "ready"]


@pytest.mark.asyncio
async def test_sink_pairs_bot_speaking_lifecycle_before_playout_drain() -> None:
    order: list[str] = []

    async def callback(name: str) -> None:
        order.append(name)

    sink = ConversationSink(
        lambda _audio, _rate: 1,
        LatencyTrace(),
        lambda: callback("user-started"),
        lambda: callback("user-stopped"),
        lambda: callback("tts-started"),
        lambda: callback("playout-drained"),
    )

    async def push(frame, _direction=FrameDirection.DOWNSTREAM) -> None:
        if isinstance(frame, BotStartedSpeakingFrame):
            order.append("bot-started")
        elif isinstance(frame, BotStoppedSpeakingFrame):
            order.append("bot-stopped")

    sink.push_frame = push  # type: ignore[method-assign]
    await sink.process_frame(TTSStartedFrame(), FrameDirection.DOWNSTREAM)
    await sink.process_frame(
        TTSAudioRawFrame(b"\0\0" * 160, 16_000, 1),
        FrameDirection.DOWNSTREAM,
    )
    await sink.process_frame(TTSStoppedFrame(), FrameDirection.DOWNSTREAM)

    assert order.count("bot-started") == 2
    assert order.count("bot-stopped") == 2
    assert order.index("bot-stopped") < order.index("playout-drained")


@pytest.mark.asyncio
async def test_downlink_drain_includes_hardware_playout_tail(monkeypatch) -> None:
    sleeps: list[float] = []

    async def fake_sleep(seconds: float) -> None:
        sleeps.append(seconds)

    monkeypatch.setattr(asyncio, "sleep", fake_sleep)
    track = DownlinkAudioTrack(LatencyTrace())
    try:
        await track.wait_drained()
    finally:
        track.stop()

    assert sleeps == [0.35]


@pytest.mark.asyncio
async def test_focused_answer_bypasses_llm_and_becomes_typed_tts() -> None:
    class Controller:
        def route_focused(self, text: str, utterance_id: str):
            assert text == "the horizontal bar"
            assert utterance_id.startswith("utt_")
            return SimpleNamespace(
                handled=True,
                answer="bar",
                job_id="job_recovered",
                question_id="layout",
            )

    thinking = False

    transcript: tuple[str, bool] | None = None

    async def on_transcript(text: str, final: bool) -> None:
        nonlocal transcript
        nonlocal thinking
        thinking = True
        transcript = (text, final)

    router = FocusRouter(Controller(), LatencyTrace(), on_transcript)  # type: ignore[arg-type]
    pushed: list[object] = []

    async def push(frame, _direction=FrameDirection.DOWNSTREAM) -> None:
        pushed.append(frame)

    router.push_frame = push  # type: ignore[method-assign]
    await router.process_frame(
        TranscriptionFrame(
            text="the horizontal bar",
            user_id="watch",
            timestamp="",
            language="en",
            finalized=False,
        ),
        FrameDirection.DOWNSTREAM,
    )

    assert thinking
    assert transcript == ("the horizontal bar", True)
    assert len(pushed) == 1
    assert isinstance(pushed[0], TTSSpeakFrame)
    assert pushed[0].text == "Got it—bar selected for that build."
