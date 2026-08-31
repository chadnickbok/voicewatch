"""Product-facing media/session contracts shared across transports."""
from __future__ import annotations

import asyncio
import hashlib
import json
from typing import Any, Protocol
from collections.abc import Awaitable, Callable

from aiohttp import web
from .metrics import LatencyTrace


class DownlinkSession(Protocol):
    def begin_downlink(self) -> None: ...
    def enqueue_downlink(self, pcm: bytes, sample_rate: int) -> int: ...
    def end_downlink(self) -> int: ...
    def clear_downlink(self) -> None: ...
    async def resume_after_downlink(self) -> None: ...


class DownlinkUtteranceBinding:
    """Keep one TTS utterance attached to the session that started it.

    Pipecat may deliver already-buffered audio after an interruption, and the
    watch may reconnect between TTS lifecycle frames. Stale chunks are dropped
    instead of being enqueued into an inactive or replacement session.
    """

    def __init__(self) -> None:
        self._session: DownlinkSession | None = None
        self._finalized = False
        self._revision = 0

    def begin(self, session: DownlinkSession | None) -> None:
        self.cancel()
        if session is None:
            return
        session.begin_downlink()
        self._session = session
        self._finalized = False

    def enqueue(
        self,
        current_session: DownlinkSession | None,
        pcm: bytes,
        sample_rate: int,
    ) -> int:
        if self._session is None:
            return 0
        if self._session is not current_session:
            self.cancel()
            return 0
        if self._finalized:
            return 0
        return self._session.enqueue_downlink(pcm, sample_rate)

    def end(self, current_session: DownlinkSession | None) -> int:
        session = self._session
        if session is None:
            return 0
        if self._finalized:
            return 0
        if session is not current_session:
            self.cancel()
            return 0
        accepted = session.end_downlink()
        self._finalized = True
        return accepted

    def release(self, current_session: DownlinkSession | None) -> None:
        """Detach a normally drained utterance without clearing its track."""

        session = self._session
        self._revision += 1
        self._session = None
        self._finalized = False
        if session is not None and session is not current_session:
            session.clear_downlink()

    def cancel(self) -> None:
        session = self._session
        self._revision += 1
        self._session = None
        self._finalized = False
        if session is not None:
            session.clear_downlink()

    async def wait_for_playback(self, current_session: Callable[[], DownlinkSession | None]) -> None:
        """An old drain may finish after a new utterance has bound this object."""
        session, revision = self._session, self._revision
        if session is None:
            return
        if session is not current_session():
            self.cancel()
            return
        try:
            await session.resume_after_downlink()
        finally:
            if revision == self._revision:
                self.release(current_session())



class WatchActionError(RuntimeError):
    def __init__(self, code: str, message: str, revision: int | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.revision = revision


class ControlSession:
    """Shared product control and action futures; no media protocol assumptions."""

    def __init__(
        self, websocket: web.WebSocketResponse, trace: LatencyTrace,
        on_audio: Callable[[str, bytes], Awaitable[None]],
        on_event: Callable[[str, str, dict[str, Any]], Awaitable[None]],
        on_identified: Callable[..., Awaitable[None]],
    ) -> None:
        self.websocket = websocket
        self.trace = trace
        self.on_audio = on_audio
        self.on_event = on_event
        self.on_identified = on_identified
        self.device_id: str | None = None
        self.board: str | None = None
        self.capabilities: dict[str, Any] = {}
        self.sequence = 0
        self.connected = asyncio.Event()
        self._pending_actions: dict[str, asyncio.Future[dict[str, Any]]] = {}
        self._closed = False

    async def send(self, message_type: str, payload: dict[str, Any] | None = None) -> None:
        self.sequence += 1
        document = {"v": 1, "type": message_type, "seq": self.sequence}
        if self.device_id is not None:
            document["device_id"] = self.device_id
        if payload is not None:
            document["payload"] = payload
        await self.websocket.send_str(json.dumps(document, separators=(",", ":")))

    async def invoke_action(
        self,
        capability: str,
        arguments: dict[str, Any],
        idempotency_key: str,
        timeout: float = 8.0,
    ) -> dict[str, Any]:
        bounded_key = idempotency_key
        if len(bounded_key.encode("utf-8")) >= 65:
            bounded_key = "idem_" + hashlib.sha256(
                bounded_key.encode("utf-8")
            ).hexdigest()[:48]
        request_id = bounded_key
        if request_id in self._pending_actions:
            raise RuntimeError("action with this idempotency key is already pending")
        future: asyncio.Future[dict[str, Any]] = asyncio.get_running_loop().create_future()
        self._pending_actions[request_id] = future
        try:
            await self.send(
                "action.invoke",
                {
                    "request_id": request_id,
                    "capability": capability,
                    "idempotency_key": bounded_key,
                    "arguments": arguments,
                },
            )
            payload = await asyncio.wait_for(future, timeout)
        finally:
            self._pending_actions.pop(request_id, None)
        if not payload.get("ok"):
            error = payload.get("error") or {}
            raise WatchActionError(
                str(error.get("code", "action_failed")),
                str(error.get("message", "Watch action failed.")),
                int(error["revision"]) if "revision" in error else None,
            )
        result = dict(payload.get("result") or {})
        result["duplicate"] = bool(payload.get("duplicate"))
        return result

    def _fail_pending_actions(self) -> None:
        for future in self._pending_actions.values():
            if not future.done():
                future.set_exception(ConnectionError("watch disconnected"))
        self._pending_actions.clear()
