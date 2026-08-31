"""Bind asynchronous provider callbacks to one originating watch session."""
from __future__ import annotations

from .session import DownlinkUtteranceBinding


class ProviderSession:
    def __init__(self, session, current_session) -> None:
        self.session = session
        self.current_session = current_session
        self.downlink = DownlinkUtteranceBinding()
        self.retired = False

    def live(self) -> bool:
        return (not self.retired and self.session is not None
                and self.current_session() is self.session
                and not getattr(self.session, '_closed', False))

    def retire(self) -> None:
        self.retired = True
        self.downlink.cancel()

    def audio(self, pcm: bytes, sample_rate: int) -> int:
        if not self.live():
            return 0
        return self.downlink.enqueue(self.session, pcm, sample_rate)

    async def stop_capture(self) -> None:
        if self.live():
            await self.session.stop_capture()

    async def begin(self) -> None:
        if self.live():
            self.downlink.begin(self.session)

    async def end(self) -> None:
        if self.live():
            self.downlink.end(self.session)

    async def wait(self) -> None:
        if self.live():
            await self.downlink.wait_for_playback(self.current_session)

    async def action(self, capability, arguments, idempotency_key):
        if not self.live():
            raise ConnectionError('provider session retired')
        result = await self.session.invoke_action(capability, arguments, idempotency_key)
        if not self.live():
            raise ConnectionError('provider session retired')
        return result
