"""Prepare an immutable native/config generation; never start or stop services."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
import time

from .moq_supervisor import SupervisorError, load_profile, write_private

LICENSES = ('audiopus_sys-LICENSE.md', 'libopus-COPYING',
            'moq-rust-LICENSE-MIT', 'moq-rust-LICENSE-APACHE')


def profile_lock(path, wait_unlocked):
    if not 0 <= wait_unlocked <= 40:
        raise SupervisorError()
    if not os.path.lexists(path):
        return None
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or meta.st_mode & 0o077 or meta.st_uid != os.getuid():
            raise SupervisorError()
        deadline = time.monotonic() + wait_unlocked
        while True:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fd
            except BlockingIOError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
    except BaseException:
        os.close(fd)
        raise


def copy_regular(source, target, limit, mode=0o600):
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as stream:
        meta = os.fstat(stream.fileno())
        if not stat.S_ISREG(meta.st_mode) or meta.st_size > limit:
            raise SupervisorError()
        with target.open('xb') as output:
            target.chmod(mode)
            remaining = limit
            while remaining:
                chunk = stream.read(min(64*1024, remaining))
                if not chunk:
                    break
                output.write(chunk)
                remaining -= len(chunk)
            if stream.read(1):
                raise SupervisorError()


def prepare(profile_path, output_path, license_dir, *, wait_unlocked=0):
    if not 0 <= wait_unlocked <= 40:
        raise SupervisorError()
    profile, host = load_profile(profile_path)
    parent = output_path.parent
    parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    meta = parent.lstat()
    if not stat.S_ISDIR(meta.st_mode) or meta.st_mode & 0o077 or meta.st_uid != os.getuid():
        raise SupervisorError()
    locked = None
    generation = None
    try:
        locked = profile_lock(output_path, wait_unlocked)
        generation = Path(tempfile.mkdtemp(prefix='generation-', dir=parent))
        for field, filename, limit in [('certificate', 'server.pem', 65536),
                ('private_key', 'server.key', 16384), ('device_keys', 'devices.json', 65536)]:
            copy_regular(Path(host[field]), generation/filename, limit)
            host[field] = str(generation/filename)
        copy_regular(Path(profile['endpoint_binary']), generation/'voicewatch-moq-endpoint',
                     256*1024*1024, mode=0o700)
        (generation/'licenses').mkdir(mode=0o700)
        for name in LICENSES:
            copy_regular(license_dir/name, generation/'licenses'/name, 65536)
        # This configured socket is a placeholder. The supervisor supplies a
        # fresh short private path for each invocation before either child runs.
        host['ipc_socket'] = str(generation/'agent.sock')
        write_private(generation/'host.json', host)
        profile.update(host_config=str(generation/'host.json'),
                       endpoint_binary=str(generation/'voicewatch-moq-endpoint'))
        write_private(generation/'supervisor.json', profile)
        load_profile(generation/'supervisor.json')  # Recheck copied binary hash and trust.
        manifest = {str(path.relative_to(generation)): hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in generation.rglob('*') if path.is_file()}
        write_private(generation/'manifest.json', manifest)
        # Same-directory rename prevents launchd reading a partially written
        # profile; prior generations remain available for explicit rollback.
        copy_regular(generation/'supervisor.json', generation/'activate.json', 16384)
        os.replace(generation/'activate.json', output_path)
        return generation
    except BaseException:
        if generation is not None:
            shutil.rmtree(generation)
        raise
    finally:
        if locked is not None:
            os.close(locked)


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--licenses', type=Path)
    parser.add_argument('--wait-stopped', type=Path,
                        help='only wait for a stopped pair before replacing Python runtime files')
    parser.add_argument('--wait-unlocked', type=float, default=0,
                        help='bounded wait after stopping a launchd job (0..40 seconds)')
    args = parser.parse_args()
    if args.wait_stopped is not None:
        if any(value is not None for value in (args.profile, args.output, args.licenses)):
            parser.error('--wait-stopped cannot prepare a deployment')
    elif any(value is None for value in (args.profile, args.output, args.licenses)):
        parser.error('preparation requires --profile, --output and --licenses')
    os.umask(0o077)
    try:
        if args.wait_stopped is not None:
            locked = profile_lock(args.wait_stopped, args.wait_unlocked)
            if locked is not None:
                os.close(locked)
            return 0
        prepare(args.profile, args.output, args.licenses, wait_unlocked=args.wait_unlocked)
    except Exception:
        print('MoQ deployment preparation failed; private details suppressed', file=sys.stderr)
        return 1
    print('MoQ deployment prepared; no service was started or stopped')
    return 0


if __name__ == '__main__':
    raise SystemExit(cli())
