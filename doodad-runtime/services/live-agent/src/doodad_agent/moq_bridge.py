"""Owner-private Unix listener binding a native MoQ peer to one live WSS grant.

This module authenticates/owns IPC, not a media gateway. The Rust peer is the
watch-facing MoQ endpoint. Application callbacks supply the capture/response
state machine and may only accept media after their own catalog/range checks.
"""
from __future__ import annotations

import asyncio
import os
import socket
import stat
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any

from .moq_auth import AuthorizationError, GrantRegistry
from .moq_ipc import IpcError, IpcWriter, Packet, read_packet


class BridgePeer:
    """One live, scoped connection. Only one application actor may write it."""

    def __init__(self, registry: GrantRegistry, owner: object,
                 grant: dict[str, Any], reader: asyncio.StreamReader, writer: IpcWriter) -> None:
        self.registry, self.owner = registry, owner
        self.session_id = str(grant['session_id'])
        self.device_id = str(grant['device_id'])
        self.publish = str(grant['publish'])
        self.subscribe = str(grant['subscribe'])
        self.reader, self.writer = reader, writer
        self._sent = 0
        self._received = 0

    async def send(self, kind: str, fields: dict[str, Any] | None = None, pcm: bytes = b'') -> None:
        if not self.registry.valid(self.session_id, self.owner):
            raise AuthorizationError()
        self._sent += 1
        await self.writer.send({**(fields or {}), 'v': 1, 'type': kind,
                                'session_id': self.session_id, 'seq': self._sent}, pcm)

    async def receive(self) -> Packet:
        packet = await read_packet(self.reader)
        seq = packet.header.get('seq')
        if (not self.registry.valid(self.session_id, self.owner)
                or packet.header.get('session_id') != self.session_id
                or type(seq) is not int or seq != self._received + 1 or seq >= 2**63):
            raise AuthorizationError()
        self._received = seq
        return packet


PeerCallback = Callable[[BridgePeer], Awaitable[None]]
PacketCallback = Callable[[BridgePeer, Packet], Awaitable[None]]


class MoqBridgeServer:
    """At most eight peers, 8 KiB input buffers and bounded callback deadlines.

    The protected directory is provisioned by the supervisor. Never unlink an
    existing socket on startup: it may belong to a live endpoint. Shutdown
    removes only the exact socket inode created by this instance.
    """

    def __init__(self, path: Path, registry: GrantRegistry, on_attach: PeerCallback,
                 on_packet: PacketCallback, on_close: PeerCallback, *, max_peers: int = 8) -> None:
        if not 1 <= max_peers <= 32:
            raise ValueError('invalid IPC peer limit')
        self.path, self.registry = path, registry
        self.on_attach, self.on_packet, self.on_close = on_attach, on_packet, on_close
        self.max_peers = max_peers
        self._server: asyncio.Server | None = None
        self._tasks: set[asyncio.Task] = set()
        self._inode: tuple[int, int] | None = None
        self._closing = False
        self.unexpected_failures = 0

    async def start(self) -> None:
        if self._server is not None or self._closing:
            raise RuntimeError('IPC server already started or closed')
        parent = self.path.parent.lstat()
        if not stat.S_ISDIR(parent.st_mode) or parent.st_mode & 0o077 or parent.st_uid != os.getuid():
            raise ValueError('IPC directory must be owner-only and not a symlink')
        if os.path.lexists(self.path):
            raise FileExistsError('IPC socket path is already occupied')
        if len(os.fsencode(self.path)) > 100:
            raise ValueError('IPC socket path must fit the macOS Unix socket limit (100 bytes)')
        # Passing path= to asyncio can unlink an existing socket during bind.
        # Bind ourselves so even a collision fails without removing anything.
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            listener.setblocking(False)
            listener.bind(str(self.path))
            meta = self.path.lstat()
            self._inode = meta.st_dev, meta.st_ino
            os.chmod(self.path, 0o600)
            self._server = await asyncio.start_unix_server(self._accept, sock=listener, limit=8192, backlog=8)
        except BaseException:
            listener.close()
            await self.close()
            raise

    def _accept(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        if self._closing or len(self._tasks) >= self.max_peers:
            writer.close()
            return
        task = asyncio.create_task(self._serve(reader, writer))
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    async def _serve(self, reader: asyncio.StreamReader, raw_writer: asyncio.StreamWriter) -> None:
        writer = IpcWriter(raw_writer)
        owner = object()
        peer: BridgePeer | None = None
        sid: str | None = None
        children: list[asyncio.Task] = []
        try:
            packet = await read_packet(reader, timeout=3)
            if packet.pcm or set(packet.header) != {'v', 'type', 'token', 'pcm_bytes'} or packet.header['type'] != 'attach':
                raise AuthorizationError()
            grant = self.registry.attach_media(packet.header['token'], owner)
            sid = str(grant['session_id'])
            peer = BridgePeer(self.registry, owner, grant, reader, writer)
            # No callback or decoded PCM is delivered before authorization.
            await writer.send({'type': 'authorized', **grant})
            await asyncio.wait_for(self.on_attach(peer), 2)
            children = [asyncio.create_task(self._consume(peer)), asyncio.create_task(self._watch(peer))]
            done, _ = await asyncio.wait(children, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                await task
        except (AuthorizationError, IpcError, TimeoutError, ConnectionError, OSError):
            pass  # Fixed closure; never log tokens, arbitrary headers or PCM.
        except Exception:
            # Do not leak arbitrary callback exceptions through asyncio's
            # unhandled-task logger. Retire the session and expose a counter.
            self.unexpected_failures += 1
        finally:
            if sid is not None:
                self.registry.revoke(sid, owner)
            for child in children:
                child.cancel()
            await asyncio.gather(*children, return_exceptions=True)
            await writer.close()
            if peer is not None:
                try:
                    await asyncio.wait_for(self.on_close(peer), 2)
                except (TimeoutError, ConnectionError, AuthorizationError, IpcError):
                    pass
                except Exception:
                    self.unexpected_failures += 1

    async def _consume(self, peer: BridgePeer) -> None:
        while True:
            packet = await peer.receive()
            # The application also enforces its bounded capture/response state.
            await asyncio.wait_for(self.on_packet(peer, packet), 2)

    async def _watch(self, peer: BridgePeer) -> None:
        while self.registry.valid(peer.session_id, peer.owner):
            await asyncio.sleep(0.1)

    async def close(self) -> None:
        self._closing = True
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        tasks = list(self._tasks)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        try:
            meta = self.path.lstat()
            if self._inode == (meta.st_dev, meta.st_ino) and stat.S_ISSOCK(meta.st_mode):
                self.path.unlink()
        except FileNotFoundError:
            pass
