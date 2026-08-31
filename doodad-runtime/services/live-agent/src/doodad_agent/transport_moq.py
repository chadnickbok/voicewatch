"""MoQ product adapter; the supervised Rust process owns native QUIC/media."""
from __future__ import annotations

import asyncio
import ssl
from pathlib import Path

from .moq_auth import GrantRegistry
from .moq_bootstrap import MoqBootstrap
from .moq_bridge import BridgePeer, MoqBridgeServer
from .moq_ipc import Packet
from .moq_session import MoqSession, MoqSessionError
from .moq_time import MoqTimeServer


class MoqTransportServer:
    def __init__(self, trace, on_audio, on_event, port: int, *,
                 registry: GrantRegistry, context: ssl.SSLContext, ipc_path: Path,
                 media_host: str, media_port: int, host: str = '0.0.0.0',
                 artifact_server=None, time_port: int | None = None) -> None:
        self.trace, self.on_audio, self.on_event = trace, on_audio, on_event
        self.registry, self.context = registry, context
        self.host, self.port, self.artifact_server = host, port, artifact_server
        self.sessions: dict[str, MoqSession] = {}
        self._pending: dict[str, MoqSession] = {}
        self._closing = False
        self.time_port = time_port
        self.time = MoqTimeServer(registry)
        self.bridge = MoqBridgeServer(ipc_path, registry, self._attach, self._packet, self._lost)
        self.bootstrap = MoqBootstrap(registry, self._control, media_host=media_host, media_port=media_port)

    async def start(self) -> None:
        await self.bridge.start()
        try:
            if self.time_port is not None:
                await self.time.start(self.host, self.time_port)
            await self.bootstrap.start(self.host, self.port, self.context,
                                       configure=self.artifact_server.add_routes if self.artifact_server else None)
        except BaseException:
            await self.time.close()
            await self.bridge.close()
            raise

    async def _control(self, websocket, sid: str, owner: object) -> None:
        if self._closing or len(self._pending) >= 8:
            return
        session = MoqSession(websocket, self.trace, self.on_audio, self.on_event,
                             self._identify, registry=self.registry, session_id=sid, owner=owner)
        self._pending[sid] = session
        try:
            await session.run()
        finally:
            self._pending.pop(sid, None)
            await session.close()
            if self.sessions.get(session.device_id) is session:
                del self.sessions[session.device_id]
                await asyncio.wait_for(self.on_event(session.device_id, 'disconnected', {}), 2)

    async def _identify(self, session: MoqSession, device_id: str, payload: dict) -> None:
        if self.registry.identity(session.session_id, session.owner) != device_id:
            raise MoqSessionError()
        prior = self.sessions.get(device_id)
        self.sessions[device_id] = session
        if prior is not None and prior is not session:
            await prior.close(code=4001, message=b'same device reconnected')
        self.trace.mark('device.identified', device_id=device_id, board=session.board)
        await self.on_event(device_id, 'identified', payload)

    async def _attach(self, peer: BridgePeer) -> None:
        session = self._pending.get(peer.session_id)
        if session is None:
            raise MoqSessionError()
        await session.attach(peer)

    async def _packet(self, peer: BridgePeer, packet: Packet) -> None:
        session = self._pending.get(peer.session_id)
        if session is None:
            raise MoqSessionError()
        await session.native(peer, packet)

    async def _lost(self, peer: BridgePeer) -> None:
        session = self._pending.get(peer.session_id)
        if session is not None and session.peer is peer:
            await session.close(code=4003, message=b'MoQ media retired')

    async def close(self) -> None:
        self._closing = True
        await asyncio.gather(*(session.close() for session in list(self._pending.values())),
                             return_exceptions=True)
        await self.bootstrap.close()
        await self.time.close()
        await self.bridge.close()
        self.sessions.clear()
        self._pending.clear()
