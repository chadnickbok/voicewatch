"""Explicit PTT ownership at the pinned Pipecat/OpenAI STT boundary.

Commit acknowledgements bind provider item IDs to the originating capture.
Only one commit may await acknowledgement; transcription completions may arrive
out of order. No audio or transcript is retained in the ownership records.
"""
from __future__ import annotations

import asyncio
import base64
import json
from contextvars import ContextVar
from dataclasses import dataclass

import numpy as np
import soxr

from pipecat.frames.frames import (
    InputAudioRawFrame, InterimTranscriptionFrame, TranscriptionFrame,
    VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame,
)
from pipecat.processors.frame_processor import FrameDirection
from pipecat.services.openai.stt import OpenAIRealtimeSTTService


CAPTURE_KEY = "voicewatch_capture"


@dataclass(eq=False)
class CaptureTurn:
    live: bool = True

    def stamp(self, frame):
        frame.metadata[CAPTURE_KEY] = self
        return frame


def frame_turn(frame) -> CaptureTurn | None:
    turn = frame.metadata.get(CAPTURE_KEY)
    return turn if isinstance(turn, CaptureTurn) else None


class CaptureAudioResampler:
    """Same VHQ streaming conversion, with an explicit per-capture tail.

    A scheduling pause must not silently clear buffered samples, and a new
    capture must never inherit the previous capture's resampler history.
    """

    def __init__(self):
        self._stream = None
        self._rates = None

    async def resample(self, audio, in_rate, out_rate):
        if in_rate == out_rate:
            return audio
        if self._stream is None:
            self._rates = (in_rate, out_rate)
            self._stream = soxr.ResampleStream(in_rate, out_rate, 1, dtype="int16", quality="VHQ")
        if self._rates != (in_rate, out_rate):
            raise ValueError("STT capture sample rate changed")
        return self._stream.resample_chunk(np.frombuffer(audio, dtype="<i2")).astype("<i2").tobytes()

    def flush(self):
        if self._stream is None:
            return b""
        result = self._stream.resample_chunk(np.empty(0, dtype="int16"), last=True)
        self._stream = None
        return result.astype("<i2").tobytes()


class CaptureRealtimeSTTService(OpenAIRealtimeSTTService):
    """Persistent STT socket with bounded, fail-closed capture correlation.

    The receive dispatch extends Pipecat 1.7.0 to consume commit acknowledgements.
    Provider configuration and STT frames remain in use, with a per-capture VHQ
    resampler. A missing acknowledgement retires the socket before another commit can
    be sent, so it cannot be mistaken for a later capture's acknowledgement.
    """

    def __init__(self, *, on_capture_failure, acknowledgement_timeout=5.0, **kwargs):
        if kwargs.get("turn_detection", False) is not False:
            raise ValueError("explicit capture requires disabled server VAD")
        if acknowledgement_timeout <= 0:
            raise ValueError("positive STT acknowledgement timeout required")
        kwargs["turn_detection"] = False
        super().__init__(**kwargs)
        self._on_capture_failure = on_capture_failure
        self._acknowledgement_timeout = acknowledgement_timeout
        self._configured = asyncio.Event()
        self._active_capture: CaptureTurn | None = None
        self._pending_commit: tuple[CaptureTurn, asyncio.Future] | None = None
        self._committed_item: tuple[str, CaptureTurn] | None = None
        self._last_item_id: str | None = None
        self._capture_broken = False
        self._frame_capture = ContextVar("stt_frame_capture", default=None)

    async def process_frame(self, frame, direction):
        scoped = direction == FrameDirection.DOWNSTREAM and isinstance(frame, (
            InputAudioRawFrame, VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame))
        if not scoped:
            await super().process_frame(frame, direction)
            return
        turn = frame_turn(frame)
        if turn is None or not turn.live:
            return
        context = self._frame_capture.set(turn)
        try:
            if isinstance(frame, VADUserStartedSpeakingFrame):
                if self._active_capture is not None and self._active_capture is not turn:
                    self._active_capture.live = False
                self._active_capture = turn
                self._committed_item = None
                self._resampler = CaptureAudioResampler()
                try:
                    await asyncio.wait_for(self._configured.wait(), self._acknowledgement_timeout)
                except TimeoutError:
                    await self._break_capture()
                    return
                if not turn.live or self._capture_broken:
                    return
                await self._clear_audio_buffer()
            if turn is self._active_capture and turn.live and not self._capture_broken:
                await super().process_frame(frame, direction)
        finally:
            self._frame_capture.reset(context)

    async def _ws_send(self, message):
        # Check again after the inherited asynchronous audio resampler, and do
        # not let the base class silently ignore an unavailable socket/send.
        if str(message.get("type", "")).startswith("input_audio_buffer."):
            turn = self._frame_capture.get()
            if turn is None or not turn.live or turn is not self._active_capture or self._capture_broken:
                return
            if self._disconnecting or self._websocket is None:
                await self._break_capture()
                return
            try:
                await asyncio.wait_for(self._websocket.send(json.dumps(message)), self._acknowledgement_timeout)
            except Exception:
                await self._break_capture()
            return
        await super()._ws_send(message)

    async def _commit_audio_buffer(self):
        turn = self._frame_capture.get()
        if turn is None or not turn.live or self._capture_broken:
            return
        if self._pending_commit is not None:
            await self._break_capture()
            return
        tail = self._resampler.flush()
        if tail:
            await self._ws_send({"type": "input_audio_buffer.append",
                                 "audio": base64.b64encode(tail).decode("ascii")})
        if not turn.live or self._capture_broken:
            return
        receipt = asyncio.get_running_loop().create_future()
        self._pending_commit = (turn, receipt)
        await super()._commit_audio_buffer()
        try:
            await asyncio.wait_for(asyncio.shield(receipt), self._acknowledgement_timeout)
        except (TimeoutError, asyncio.CancelledError):
            await self._break_capture()
            if asyncio.current_task().cancelling():
                raise
        finally:
            if self._pending_commit is not None and self._pending_commit[1] is receipt:
                self._pending_commit = None

    async def _handle_audio_committed(self, event):
        item = event.get("item_id")
        pending = self._pending_commit
        # The previous-item chain also catches an unsolicited/duplicate commit.
        if (pending is None or pending[1].done() or not isinstance(item, str)
                or not 1 <= len(item) <= 256 or item == self._last_item_id
                or event.get("previous_item_id") != self._last_item_id):
            await self._break_capture()
            return
        self._last_item_id = item
        turn, receipt = pending
        if turn.live:
            self._committed_item = (item, turn)
        receipt.set_result(True)

    def _event_capture(self, event):
        bound = self._committed_item
        if (bound is None or not bound[1].live or event.get("item_id") != bound[0]
                or event.get("content_index") != 0 or self._capture_broken):
            return None
        return bound[1]

    async def _handle_transcription_delta(self, event):
        if self._event_capture(event) is not None:
            await super()._handle_transcription_delta(event)

    async def _handle_transcription_completed(self, event):
        bound = self._committed_item
        if self._event_capture(event) is None:
            return
        try:
            await super()._handle_transcription_completed(event)
        finally:
            if self._committed_item is bound:
                self._committed_item = None

    async def _handle_transcription_failed(self, event):
        if self._event_capture(event) is not None:
            await self._break_capture()

    async def _handle_error(self, event):
        # Never put provider-supplied error text into application diagnostics.
        await self._break_capture()

    async def push_frame(self, frame, direction=FrameDirection.DOWNSTREAM):
        if isinstance(frame, (InterimTranscriptionFrame, TranscriptionFrame)):
            # App-authored text is stamped at ingress; provider frames acquire
            # their stamp only from an acknowledged provider item below.
            turn = frame_turn(frame)
            if turn is None:
                turn = self._event_capture(frame.result) if isinstance(frame.result, dict) else None
            if turn is None or not turn.live:
                return
            turn.stamp(frame)
        await super().push_frame(frame, direction)

    async def _handle_session_updated(self, event):
        await super()._handle_session_updated(event)
        self._configured.set()

    async def _break_capture(self, *, close_socket=True):
        socket = self._websocket
        turn = self._active_capture
        was_live = turn is not None and turn.live
        if turn is not None:
            turn.live = False
        self._capture_broken = True
        self._configured.clear()
        self._committed_item = None
        if self._pending_commit is not None:
            pending_turn, receipt = self._pending_commit
            pending_turn.live = False
            if not receipt.done():
                receipt.set_result(False)
        if was_live:
            await self._on_capture_failure(turn)
        if close_socket and socket is not None:
            await socket.close(code=1011, reason="capture STT failure")

    async def _receive_messages(self):
        # Keep the socket local: no result from a replaced socket may be read as
        # part of its successor. Normal turns do not reconnect any provider.
        socket = self._websocket
        assert socket is not None
        self._capture_broken = False
        self._last_item_id = None
        handlers = {
            "session.created": self._handle_session_created,
            "session.updated": self._handle_session_updated,
            "input_audio_buffer.committed": self._handle_audio_committed,
            "conversation.item.input_audio_transcription.delta": self._handle_transcription_delta,
            "conversation.item.input_audio_transcription.completed": self._handle_transcription_completed,
            "conversation.item.input_audio_transcription.failed": self._handle_transcription_failed,
            "error": self._handle_error,
        }
        try:
            async for message in socket:
                event = json.loads(message)
                if not isinstance(event, dict):
                    raise ValueError("invalid STT event")
                kind = event.get("type")
                if kind in ("input_audio_buffer.speech_started", "input_audio_buffer.speech_stopped"):
                    # Server VAD is forbidden; provider error text is not logged.
                    await self._break_capture()
                    return
                handler = handlers.get(kind)
                if handler is not None:
                    await handler(event)
                if self._capture_broken:
                    return
        finally:
            await self._break_capture(close_socket=False)
