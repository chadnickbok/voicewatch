"""One launchd job owns Python control/providers and the native MoQ endpoint.

Any child exit retires the pair. launchd, not an inner retry loop, restarts it.
Every incarnation gets a fresh owner-private IPC directory, never an unlink of
another process's socket. Configuration errors disclose no private values.
"""
from __future__ import annotations

import argparse
import asyncio
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import signal
import socket
import stat
import sys
import tempfile

from .moq_config import MoqHostConfig, _private
from .moq_ipc import _unique_object
from .moq_supervision import PARENT_FD


class SupervisorError(ValueError):
    def __init__(self):
        super().__init__('MoQ supervision failed; verify private configuration and child services')


def load_profile(path):
    try:
        value = json.loads(_private(path), object_pairs_hook=_unique_object)
        if not isinstance(value, dict) or set(value) != {
                'host_config', 'endpoint_binary', 'endpoint_sha256', 'port', 'database', 'trace'}:
            raise SupervisorError()
        if type(value['port']) is not int or not 1 <= value['port'] <= 65535:
            raise SupervisorError()
        if not isinstance(value['endpoint_sha256'], str) or not re.fullmatch('[0-9a-f]{64}', value['endpoint_sha256']):
            raise SupervisorError()
        for key in ('host_config', 'endpoint_binary', 'database', 'trace'):
            text = value[key]
            if not isinstance(text, str) or not Path(text).is_absolute() or len(os.fsencode(text)) > 1024:
                raise SupervisorError()
        binary = Path(value['endpoint_binary'])
        fd = os.open(binary, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        with os.fdopen(fd, 'rb') as stream:
            meta = os.fstat(stream.fileno())
            if (not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid()
                    or meta.st_mode & 0o022 or not os.access(binary, os.X_OK)
                    or not 0 < meta.st_size <= 256 * 1024 * 1024
                    or hashlib.file_digest(stream, 'sha256').hexdigest() != value['endpoint_sha256']):
                raise SupervisorError()
        host_path = Path(value['host_config'])
        MoqHostConfig.load(host_path)
        host = json.loads(_private(host_path), object_pairs_hook=_unique_object)
        if host.get('time_port') == value['port']:
            raise SupervisorError()
        return value, host
    except Exception:
        raise SupervisorError() from None


def write_private(path, value):
    with path.open('x') as stream:
        path.chmod(0o600)
        json.dump(value, stream)


class PairSupervisor:
    def __init__(self, profile, host, *, agent_command=None, startup_timeout=60, shutdown_timeout=30):
        self.profile, self.host = profile, host
        self.agent_command = agent_command or [sys.executable, '-m', 'doodad_agent']
        self.startup_timeout, self.shutdown_timeout = startup_timeout, shutdown_timeout
        self.children = []
        self.channels = []
        self.runtime_dir = None
        self.ready = asyncio.Event()

    async def _start(self, command, *, native=False):
        parent, child = socket.socketpair()
        parent.setblocking(False)
        # The native endpoint needs no provider, signing or SMTP credentials.
        env = ({key: os.environ[key] for key in ('PATH', 'LANG', 'LC_ALL', 'TZ') if key in os.environ}
               if native else dict(os.environ))
        env[PARENT_FD] = str(child.fileno())
        self.channels.append(parent)
        try:
            process = await asyncio.create_subprocess_exec(
                *command, env=env, pass_fds=(child.fileno(),), stdin=asyncio.subprocess.DEVNULL)
            self.children.append(process)
        finally:
            child.close()
        ready = asyncio.create_task(asyncio.get_running_loop().sock_recv(parent, 1))
        # Monitor the first child while the second starts too; a wedged second
        # startup must not delay retirement after control/provider death.
        failures = [asyncio.create_task(process.wait()) for process in self.children]
        failures += [asyncio.create_task(asyncio.get_running_loop().sock_recv(channel, 1))
                     for channel in self.channels[:-1]]
        tasks = [ready, *failures]
        try:
            async with asyncio.timeout(self.startup_timeout):
                done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            if ready not in done or any(task in done for task in failures) or ready.result() != b'R':
                raise SupervisorError()
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)

    async def _run(self, directory):
        self.runtime_dir = directory
        host = {**self.host, 'ipc_socket': str(directory/'agent.sock')}
        endpoint = dict(listen=f'0.0.0.0:{host["media_port"]}', certificate=host['certificate'],
                        private_key=host['private_key'], ipc_socket=host['ipc_socket'])
        write_private(directory/'host.json', host)
        write_private(directory/'endpoint.json', endpoint)
        await self._start([*self.agent_command, 'serve', '--transport', 'moq', '--moq-config',
                           str(directory/'host.json'), '--port', str(self.profile['port']),
                           '--database', self.profile['database'], '--trace', self.profile['trace']])
        await self._start([self.profile['endpoint_binary'], '--config', str(directory/'endpoint.json')], native=True)
        self.ready.set()
        print('MoQ supervised pair ready; media authorization remains device-scoped', flush=True)
        watchers = [asyncio.create_task(child.wait()) for child in self.children]
        watchers += [asyncio.create_task(asyncio.get_running_loop().sock_recv(channel, 1))
                     for channel in self.channels]
        try:
            await asyncio.wait(watchers, return_when=asyncio.FIRST_COMPLETED)
            raise SupervisorError()
        finally:
            for task in watchers:
                task.cancel()
            await asyncio.gather(*watchers, return_exceptions=True)

    async def _stop(self):
        # Close lifelines before waiting. Even SIGKILL of the supervisor closes
        # these descriptors, allowing children to retire their own connections.
        for channel in self.channels:
            channel.close()
        for child in self.children:
            if child.returncode is None:
                try:
                    child.terminate()
                except ProcessLookupError:
                    pass
        try:
            async with asyncio.timeout(self.shutdown_timeout):
                await asyncio.gather(*(child.wait() for child in self.children))
        except TimeoutError:
            for child in self.children:
                if child.returncode is None:
                    try:
                        child.kill()
                    except ProcessLookupError:
                        pass
            await asyncio.gather(*(child.wait() for child in self.children))

    async def run(self, stopped):
        # A unique short directory fits macOS sun_path even with long profile
        # paths. Never scan, clean or reuse directories from prior invocations.
        with tempfile.TemporaryDirectory(prefix='vw-moq-', dir='/tmp') as temporary:
            operation = asyncio.create_task(self._run(Path(temporary)))
            shutdown = asyncio.create_task(stopped.wait())
            try:
                done, _ = await asyncio.wait((operation, shutdown), return_when=asyncio.FIRST_COMPLETED)
                if operation in done:
                    await operation
            finally:
                operation.cancel()
                shutdown.cancel()
                await asyncio.gather(operation, shutdown, return_exceptions=True)
                await self._stop()


async def serve(profile_path):
    # Lock the existing private profile without truncating it or creating a
    # world-readable sidecar. A second invocation cannot replace a live pair.
    fd = os.open(profile_path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        profile, host = load_profile(profile_path)
        stopped = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, stopped.set)
        await PairSupervisor(profile, host).run(stopped)
    finally:
        os.close(fd)


def cli():
    parser = argparse.ArgumentParser(description='Supervise the explicit MoQ service pair')
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--check', action='store_true', help='validate configuration without starting services')
    arguments = parser.parse_args()
    os.umask(0o077)
    try:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        if arguments.check:
            load_profile(arguments.config)
        else:
            asyncio.run(serve(arguments.config))
    except Exception:
        print(str(SupervisorError()), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(cli())
