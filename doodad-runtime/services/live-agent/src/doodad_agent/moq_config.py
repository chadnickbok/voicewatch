"""Private configuration for the explicit MoQ service mode, without fallback."""
from __future__ import annotations

import json
import os
import re
import ssl
import stat
from dataclasses import dataclass
from pathlib import Path

from .moq_auth import GrantRegistry, load_device_keys
from .moq_ipc import _unique_object


class MoqConfigError(ValueError):
    def __init__(self) -> None:
        super().__init__('invalid private MoQ host configuration')


def _private(path: Path) -> bytes:
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if (not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid()
                or meta.st_mode & 0o077 or meta.st_size > 16384):
            raise MoqConfigError()
        data = os.read(fd, 16385)
        if len(data) > 16384:
            raise MoqConfigError()
        return data
    finally:
        os.close(fd)


@dataclass(repr=False)
class MoqHostConfig:
    registry: GrantRegistry
    context: ssl.SSLContext
    ipc_path: Path
    public_host: str
    media_port: int
    time_port: int | None = None

    @classmethod
    def load(cls, path: Path) -> MoqHostConfig:
        try:
            config = json.loads(_private(path), object_pairs_hook=_unique_object)
            if (not isinstance(config, dict) or set(config) - {'time_port'} != {
                    'certificate', 'private_key', 'device_keys', 'ipc_socket', 'public_host', 'media_port'}
                    or not isinstance(config['public_host'], str)
                    or not re.fullmatch(r'[a-zA-Z0-9.-]{1,253}', config['public_host'])
                    or type(config['media_port']) is not int or not 1 <= config['media_port'] <= 65535):
                raise MoqConfigError()
            time_port = config.get('time_port')
            if time_port is not None and (type(time_port) is not int or not 1 <= time_port <= 65535):
                raise MoqConfigError()
            paths = {}
            for name in ('certificate', 'private_key', 'device_keys', 'ipc_socket'):
                value = config[name]
                if not isinstance(value, str) or len(value.encode()) > 1024 or not Path(value).is_absolute():
                    raise MoqConfigError()
                paths[name] = Path(value)
            _private(paths['private_key'])
            registry = GrantRegistry(load_device_keys(paths['device_keys']))
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            certificate = paths['certificate'].lstat()
            if not stat.S_ISREG(certificate.st_mode) or certificate.st_size > 65536:
                raise MoqConfigError()
            # Never prompt on stdin if an encrypted key is supplied by mistake.
            context.load_cert_chain(paths['certificate'], paths['private_key'], password=lambda: '')
            return cls(registry, context, paths['ipc_socket'], config['public_host'], config['media_port'], time_port)
        except Exception:
            # No filenames, enrollment data, PEM or JSON values in errors.
            raise MoqConfigError() from None
