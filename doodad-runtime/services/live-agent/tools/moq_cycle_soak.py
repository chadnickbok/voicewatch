#!/usr/bin/env python3
"""Bounded, host-triggered capture/decode/re-encode/playback lifecycle fixture.

One second of microphone PCM is held in RAM for its echo, never persisted or
sent to a provider. This exercises media lifecycle, not physical button input
or calibrated speech quality. The serial observer issues only read-only STATS.
"""
import argparse
import asyncio
import os
from pathlib import Path
import sys
import time

from moq_idle_soak import FAULTS, ROOT, process_rss, statuses, validate_idle_status

CAPTURE_MS = 1000
SAMPLES = 16000
PCM_BYTES = SAMPLES * 2


class EchoCapture:
    """At most one bounded capture; repr/logs never include its PCM or identity."""

    def __init__(self):
        self.session = self.capture = None
        self.completed = asyncio.Event()
        self.pcm = bytearray()
        self.armed = False
        self.high_capture = 0

    def arm(self, session):
        if self.armed or self.pcm:
            raise RuntimeError('echo capture was not released')
        self.session, self.capture = session, None
        self.completed.clear()
        self.armed = True

    def audio(self, pcm):
        if (not self.armed or self.capture is None or self.completed.is_set()
                or self.session._capture is not self.capture or len(pcm) % 2
                or len(self.pcm) + len(pcm) > PCM_BYTES):
            raise RuntimeError('echo PCM ownership or bound failed')
        self.pcm.extend(pcm)

    def event(self, kind, payload):
        if kind == 'capture.failed':
            raise RuntimeError('echo capture failed')
        if kind not in ('capture.started', 'capture.stopped'):
            return
        if not self.armed:
            raise RuntimeError('unrequested echo capture event')
        current = self.session._capture
        if current is None or any(payload.get(key) != value for key, value in current.identity.fields().items()):
            raise RuntimeError('echo capture identity mismatch')
        if kind == 'capture.started':
            if self.capture is not None or int(current.identity.capture_id) <= self.high_capture:
                raise RuntimeError('echo capture identity was reused')
            self.capture = current
            self.high_capture = int(current.identity.capture_id)
        elif (current is not self.capture or self.completed.is_set() or not current.validated.is_set()
              or int(payload.get('samples', -1)) != SAMPLES or current.received != SAMPLES
              or len(self.pcm) != PCM_BYTES):
            raise RuntimeError('echo capture receipt incomplete')
        else:
            self.completed.set()

    def clear(self):
        # Release the fixture's mutable copy on success, error and cancellation.
        # Python/the codec may hold transient copies; do not claim secure erasure.
        self.pcm[:] = bytes(len(self.pcm))
        self.pcm.clear()
        self.session = self.capture = None
        self.armed = False


def serial_monitor(args):
    sys.path.insert(0, str(ROOT / 'doodad-runtime/tools'))
    from moq_enroll import connect, send
    os.umask(0o077)
    descriptor = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    until = time.monotonic() + args.seconds
    pending = b''
    previous_uptime = -1
    with os.fdopen(descriptor, 'wb') as log, connect(args.port) as link:
        query_at = 0.0
        while time.monotonic() < until:
            if time.monotonic() >= query_at:
                send(link, b'VWMOQ1 STATS\n')
                query_at = time.monotonic() + 1
            chunk = link.read(4096)
            log.write(chunk)
            log.flush()
            pending += chunk
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                raw = line.decode('utf-8', errors='replace')
                if any(marker in raw for marker in FAULTS):
                    raise RuntimeError('firmware fault during cycle observation')
                for row in statuses(raw + '\n'):
                    if row['uptime_ms'] <= previous_uptime:
                        raise RuntimeError('firmware status clock regressed')
                    previous_uptime = row['uptime_ms']
            pending = pending[-8192:]


async def run_cycles(*, count, session, collector, result, directory, native, monitor,
                     check_session, mark, clock=time.monotonic, pause=asyncio.sleep,
                     take_snapshot=None):
    if count not in (3, 1000):
        raise ValueError('cycle fixture must run three smoke or 1000 optional endurance cycles')
    started = clock()
    initial_renewals = session.renewals_completed
    state = dict(requested=count, completed=0, cycles=[], snapshots=[],
                 host_triggered=True, physical_button_verified=False,
                 pcm_persisted=False, pcm_capacity_bytes=PCM_BYTES, protocol_pass=False,
                 cumulative_heap_recovery_verified=False)
    result['echo_cycles'] = state

    def check():
        check_session()
        if (session._closed or session._fault.is_set() or not session.connected.is_set()
                or native.returncode is not None or monitor.returncode is not None):
            raise RuntimeError('cycle session or process invariant failed')

    async def snapshot():
        path = directory / 'serial.log'
        rows = statuses(path.read_text(errors='replace')) if path.exists() else []
        watermark = rows[-1]['uptime_ms'] if rows else 0
        async with asyncio.timeout(8):
            while True:
                check()
                rows = statuses(path.read_text(errors='replace')) if path.exists() else []
                if rows and rows[-1]['uptime_ms'] > watermark:
                    row = rows[-1]
                    # Publication/drain acknowledgements can precede the final
                    # ownership release. Wait for a fresh, fully idle snapshot.
                    if not any(row[key] for key in ('microphone', 'speaker', 'publish', 'receive',
                                                   'leased', 'tx_queued', 'rx_owned')):
                        validate_idle_status(row)
                        row.update(elapsed_ms=round((clock()-started)*1000), after_cycle=state['completed'],
                                   native_rss_kib=await process_rss(native), service_rss_kib=await process_rss())
                        return row
                await pause(.1)

    observe = take_snapshot or snapshot
    try:
        check()
        state['snapshots'].append(await observe())
        high_response = 0
        for index in range(count):
            check()
            began = clock()
            collector.arm(session)
            await session.start_capture(CAPTURE_MS)
            async with asyncio.timeout(CAPTURE_MS/1000 + 10):
                while not collector.completed.is_set():
                    check()
                    await pause(.02)
            capture = collector.capture
            captured_ms = round((clock()-began)*1000)
            check()
            session.begin_downlink()
            response = session._response
            if response is None or response.context is not capture or response.number <= high_response:
                raise RuntimeError('echo response identity mismatch')
            high_response = response.number
            session.enqueue_downlink(collector.pcm, 16000)
            session.end_downlink()
            collector.clear()
            if not await asyncio.wait_for(session.resume_after_downlink(), 12):
                raise RuntimeError('echo playback cancelled or replaced')
            check()
            if (session._response is not response or response.context is not capture
                    or response.cancelled or not response.finished.is_set() or response.samples != SAMPLES):
                raise RuntimeError('echo playback receipt incomplete')
            # Explicitly retire the completed context, then require the device
            # to report no retained media/audio ownership before the next turn.
            session._retire_capture()
            state['completed'] += 1
            row = await observe()
            state['snapshots'].append(row)
            state['cycles'].append(dict(index=index+1, microphone_samples=SAMPLES, speaker_samples=SAMPLES,
                capture_ms=captured_ms, cycle_ms=round((clock()-began)*1000),
                renewals=session.renewals_completed-initial_renewals))
            mark('echo_cycle_completed', cycle=index+1, cycles=count,
                 cycle_ms=state['cycles'][-1]['cycle_ms'], renewals=session.renewals_completed-initial_renewals)
        if count == 1000 and session.renewals_completed <= initial_renewals:
            raise RuntimeError('cycle workload did not renew credentials')
        check()
        state['elapsed_ms'] = round((clock()-started)*1000)
        state['renewals'] = session.renewals_completed-initial_renewals
        state['protocol_pass'] = True
    finally:
        collector.clear()
        if not state['protocol_pass']:
            session._retire_capture()
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('monitor',))
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 18000:
        parser.error('monitor duration must be 1..18000 seconds')
    try:
        serial_monitor(args)
    except Exception as error:
        raise SystemExit('Cycle monitor failed (' + type(error).__name__ + '); inspect private evidence') from None


if __name__ == '__main__':
    main()
