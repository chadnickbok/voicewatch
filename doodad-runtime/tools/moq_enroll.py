#!/usr/bin/env python3
"""Physical USB enrollment for the Ultra; run with ESP-IDF Python (pyserial).

Never flashes or erases firmware. Profile/key material and raw serial stay in
owner-private files, never arguments or terminal output. No firmware restore.
"""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import time

import serial


def private_read(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid() or meta.st_mode & 0o077 or meta.st_size > 8192:
            raise ValueError('profile must be an owner-private regular file of at most 8192 bytes')
        return os.read(fd, 8193)
    finally:
        os.close(fd)


def private_write(path, value):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream)
        stream.write('\n')


def connect(port):
    link = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=2)
    # Same open sequence as IDF monitor; no transient download/reset pulse.
    link.rts = link.dtr = True
    link.port = port
    link.open()
    link.rts = link.dtr = False
    return link


def send(link, command):
    # The console may use the 64-byte hardware RX FIFO, not a driver ring.
    for offset in range(0, len(command), 32):
        link.write(command[offset:offset+32])
        time.sleep(.01)
    link.flush()


def response(link, pattern, seconds=8):
    deadline = time.monotonic() + seconds
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(link.read(2048))
        match = re.search(pattern, pending)
        if match:
            return match
        if any(marker in pending for marker in (b'Guru Meditation', b'abort() was called', b'assert failed')):
            raise RuntimeError('firmware fault during USB enrollment')
        del pending[:-8192]
    raise TimeoutError('USB enrollment response timeout')


def info(link):
    send(link, b'VWMOQ1 INFO\n')
    reply = response(link, rb'VWMOQ1 INFO device=([a-zA-Z0-9_-]{1,64}) revision=([0-9]+)\r?\n')
    return dict(device_id=reply[1].decode('ascii'), revision=int(reply[2]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('info', 'install', 'monitor'))
    parser.add_argument('--port', required=True)
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=90)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('monitor duration out of range')
    os.umask(0o077)
    with connect(args.port) as link:
        if args.command == 'monitor':
            with args.output.open('xb') as stream:
                until = time.monotonic() + args.seconds
                while time.monotonic() < until:
                    stream.write(link.read(4096))
                    stream.flush()
            return
        current = info(link)
        if args.command == 'info':
            private_write(args.output, current)
            print('USB enrollment identity recorded privately', flush=True)
            return
        if args.profile is None:
            parser.error('install requires --profile')
        profile = json.loads(private_read(args.profile))
        if (profile.get('device_id') != current['device_id'] or type(profile.get('revision')) is not int
                or not current['revision'] < profile['revision'] <= 0xffffffff):
            raise ValueError('profile must match the connected device and advance its revision')
        wire = json.dumps(profile, separators=(',', ':'), ensure_ascii=True).encode('ascii')
        if len(wire) > 8180:
            raise ValueError('profile exceeds USB command bound')
        send(link, b'VWMOQ1 SET ' + wire + b'\n')
        ack = response(link, rb'VWMOQ1 (OK|DENIED) revision=([0-9]+)\r?\n')
        if ack[1] != b'OK' or int(ack[2]) != profile['revision']:
            raise RuntimeError('device rejected enrollment')
        private_write(args.output, dict(installed=True, revision=int(ack[2])))
        print('USB enrollment installed', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Exception strings can contain serial buffers or JSON/key snippets.
        raise SystemExit('USB enrollment failed (' + type(error).__name__ + '); no secret details logged') from None
