"""Inherited owner channel: readiness byte followed by parent-lifetime EOF.

No network health endpoint or token is involved. The descriptor is passed only
to the supervised child and is closed on exec so provider workers cannot keep
an orphaned service alive. Standalone service operation remains supported.
"""
import asyncio
import os
import socket

PARENT_FD = 'DOODAD_MOQ_SUPERVISOR_FD'


class ParentLink:
    def __init__(self, sock):
        self.sock = sock
        self.task = None

    @classmethod
    def inherited(cls):
        value = os.environ.pop(PARENT_FD, None)
        if value is None:
            return None
        if not value.isascii() or not value.isdecimal() or not 3 <= int(value) < 2**31:
            raise ValueError('invalid MoQ supervisor descriptor')
        sock = socket.socket(fileno=int(value))
        if sock.family != socket.AF_UNIX or sock.type != socket.SOCK_STREAM:
            sock.close()
            raise ValueError('invalid MoQ supervisor channel')
        sock.set_inheritable(False)
        sock.setblocking(False)
        return cls(sock)

    async def ready(self, stopped):
        await asyncio.get_running_loop().sock_sendall(self.sock, b'R')

        async def watch():
            try:
                # The parent sends no commands. EOF or unexpected data both
                # retire this incarnation instead of preserving stale grants.
                await asyncio.get_running_loop().sock_recv(self.sock, 1)
            finally:
                stopped.set()

        self.task = asyncio.create_task(watch())

    async def close(self):
        if self.task is not None:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
        self.sock.close()
