"""Bounded local IPC for the supervised native MoQ endpoint.

Wire v1: big-endian u32 JSON byte length, UTF-8 JSON object, then exactly
pcm_bytes bytes of little-endian mono PCM16k. No audio/base64 in JSON/logs.
The 4 KiB metadata and 20 ms PCM limits are checked before allocation/read.
A connection carries one redeemed control session and dies with that session.
"""
from __future__ import annotations

import asyncio
import json
import struct
from dataclasses import dataclass, field
from typing import Any

MAX_HEADER = 4096
MAX_PCM = 640
READ_TIMEOUT = 30.0
WRITE_TIMEOUT = 2.0


class IpcError(Exception):
    def __init__(self) -> None:
        super().__init__("invalid or stalled MoQ IPC")


@dataclass(repr=False, frozen=True)
class Packet:
    header: dict[str, Any] = field(repr=False)
    pcm: bytes = field(default=b"", repr=False)


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise IpcError()
        result[key] = value
    return result


def _validate(header: object) -> int:
    if not isinstance(header, dict) or type(header.get("v")) is not int or header["v"] != 1:
        raise IpcError()
    size = header.get("pcm_bytes", 0)
    kind = header.get("type")
    if (type(size) is not int or not 0 <= size <= MAX_PCM or size % 2
            or not isinstance(kind, str) or not 1 <= len(kind) <= 32
            or (size and kind not in {"capture.pcm", "playback.pcm"})):
        raise IpcError()
    return size


def encode(packet: Packet) -> bytes:
    size = _validate(packet.header)
    if size != len(packet.pcm):
        raise IpcError()
    try:
        header = json.dumps(packet.header, ensure_ascii=True, separators=(",", ":"), allow_nan=False).encode("ascii")
    except (TypeError, ValueError, RecursionError):
        raise IpcError() from None
    if not 1 <= len(header) <= MAX_HEADER:
        raise IpcError()
    return struct.pack("!I", len(header)) + header + packet.pcm


async def read_packet(reader: asyncio.StreamReader, *, timeout: float = READ_TIMEOUT) -> Packet:
    try:
        async with asyncio.timeout(timeout):
            length = struct.unpack("!I", await reader.readexactly(4))[0]
            if not 1 <= length <= MAX_HEADER:
                raise IpcError()
            raw = await reader.readexactly(length)
            header = json.loads(raw, object_pairs_hook=_unique_object,
                                parse_constant=lambda _: (_ for _ in ()).throw(IpcError()))
            size = _validate(header)
            return Packet(header, await reader.readexactly(size))
    except (TimeoutError, asyncio.IncompleteReadError, UnicodeError, ValueError, RecursionError):
        raise IpcError() from None


class IpcWriter:
    """One serialized writer; no unbounded queue of audio or fire-and-forget sends."""

    def __init__(self, writer: asyncio.StreamWriter) -> None:
        self.writer = writer
        self._busy = False
        writer.transport.set_write_buffer_limits(high=MAX_HEADER + MAX_PCM + 4, low=0)

    async def send(self, header: dict[str, Any], pcm: bytes = b"") -> None:
        # A caller must use one bounded actor/queue. Waiting on an unbounded
        # population of lock waiters would defeat this memory bound.
        if self._busy or self.writer.is_closing():
            raise IpcError()
        self._busy = True
        try:
            data = encode(Packet({"v": 1, **header, "pcm_bytes": len(pcm)}, pcm))
            self.writer.write(data)
            await asyncio.wait_for(self.writer.drain(), WRITE_TIMEOUT)
        except (TimeoutError, ConnectionError, OSError):
            self.writer.close()
            raise IpcError() from None
        finally:
            self._busy = False

    async def close(self) -> None:
        self.writer.close()
        try:
            await asyncio.wait_for(self.writer.wait_closed(), WRITE_TIMEOUT)
        except (TimeoutError, ConnectionError, OSError):
            self.writer.transport.abort()
