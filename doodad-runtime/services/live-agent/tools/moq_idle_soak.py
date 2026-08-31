#!/usr/bin/env python3
"""Private idle/reconnect endurance support; no audio or provider commands.

The serial subcommand runs under ESP-IDF Python and requires a test firmware
with the read-only VWMOQ1 STATS command. The host coroutine runs under the
live-agent environment. Raw serial remains private. An outer runner must
reapply permanent enrollment in its finally block after any private bench.
"""
import argparse
import asyncio
import os
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
STATUS_FIELDS = ('uptime_ms','ready','microphone','speaker','publish','receive',
                 'leased','tx_queued','internal_free','internal_min','internal_largest',
                 'psram_free','audio_stack','network_stack','dns_stack','rx_owned','rx_high','tx_high')
STATUS = re.compile('MOQ_STATUS ' + ' '.join(key+r'=(\d+)' for key in STATUS_FIELDS)
                    + r'(?:\r?\n|\x1b\[[0-9;]*m)')
FAULTS = ('Guru Meditation','abort() was called','assert failed')


def statuses(raw):
    return [dict(zip(STATUS_FIELDS,map(int,values))) for values in STATUS.findall(raw)]


def validate_idle_status(status):
    if (status['ready']!=1 or any(status[key] for key in
            ('microphone','speaker','publish','receive','leased','tx_queued','rx_owned'))):
        raise RuntimeError('idle audio or media ownership was active')
    if status['internal_min']<96*1024 or status['internal_largest']<32*1024:
        raise RuntimeError('idle snapshot below declared internal RAM floor')
    if any(status[key]==0 for key in ('audio_stack','network_stack','dns_stack')):
        raise RuntimeError('idle owner stack exhausted')


def serial_monitor(args):
    sys.path.insert(0,str(ROOT/'doodad-runtime/tools'))
    from moq_enroll import connect,send
    os.umask(0o077)
    descriptor=os.open(args.output,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    until=time.monotonic()+args.seconds
    pending=b''
    with os.fdopen(descriptor,'wb') as log, connect(args.port) as link:
        query_at=0.0
        while time.monotonic()<until:
            if time.monotonic()>=query_at:
                send(link,b'VWMOQ1 STATS\n')
                query_at=time.monotonic()+5
            chunk=link.read(4096)
            log.write(chunk);log.flush();pending+=chunk
            while b'\n' in pending:
                line,pending=pending.split(b'\n',1)
                raw=line.decode('utf-8',errors='replace')
                if any(marker in raw for marker in FAULTS):
                    raise RuntimeError('firmware fault during idle observation')
                for status in statuses(raw+'\n'):
                    # A planned reconnect can temporarily deny STATS. Any
                    # delivered audio-active snapshot is nevertheless a fault.
                    if any(status[key] for key in ('microphone','speaker','publish','receive','leased','tx_queued','rx_owned')):
                        raise RuntimeError('unexpected audio during idle observation')
            pending=pending[-8192:]


async def process_rss(process=None):
    pid=process.pid if process is not None else os.getpid()
    probe=await asyncio.create_subprocess_exec('ps','-o','rss=','-p',str(pid),
        stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.DEVNULL)
    try:
        raw,_=await asyncio.wait_for(probe.communicate(),2)
    finally:
        if probe.returncode is None:
            probe.kill();await probe.wait()
    if probe.returncode or not raw.strip().isdigit() or (process is not None and process.returncode is not None):
        raise RuntimeError('native process observation failed')
    return int(raw)


async def run_idle(*,seconds,server,first,ready,device_id,result,directory,native,monitor,capture_started,mark,
                   clock=time.monotonic,pause=asyncio.sleep):
    if seconds not in (120,28800):
        raise ValueError('idle run must be the two-minute smoke or eight-hour gate')
    started=clock()
    interval=60 if seconds==120 else 3600
    next_reconnect=started+interval
    next_sample=started
    current=first
    state=dict(requested_seconds=seconds,elapsed_ms=0,reconnects=[],renewals_per_session=[],
        snapshots=[],microphone_opened=False,protocol_pass=False,
        cumulative_heap_recovery_verified=False)
    result['idle']=state
    mark('idle_soak_started',seconds=seconds,reconnect_interval_seconds=interval)

    def check_session():
        if (current._closed or current._fault.is_set() or not current.connected.is_set()
                or server.sessions.get(device_id) is not current or not ready.empty()
                or result['microphone_samples'] or capture_started.is_set()
                or native.returncode is not None or monitor.returncode is not None
                or server.bridge.unexpected_failures or server.bootstrap.unexpected_failures):
            raise RuntimeError('idle session, capture or process invariant failed')

    async def snapshot():
        async with asyncio.timeout(12):
            while True:
                check_session()
                path=directory/'serial.log'
                rows=statuses(path.read_text(errors='replace')) if path.exists() else []
                previous=state['snapshots'][-1]['uptime_ms'] if state['snapshots'] else 0
                if rows and rows[-1]['uptime_ms']>previous and time.time()-path.stat().st_mtime<10:
                    status=rows[-1]
                    validate_idle_status(status)
                    if any(right['uptime_ms']<=left['uptime_ms'] for left,right in zip(rows,rows[1:])):
                        raise RuntimeError('firmware restarted or status clock regressed')
                    status.update(elapsed_ms=round((clock()-started)*1000),
                        native_rss_kib=await process_rss(native),service_rss_kib=await process_rss(),
                        renewals=current.renewals_completed)
                    state['snapshots'].append(status)
                    return
                await pause(.2)

    while clock()-started<seconds:
        check_session()
        now=clock()
        if now>=next_reconnect and started+seconds-now>15:
            if current.renewals_completed<1:
                raise RuntimeError('idle session never renewed before reconnect')
            state['renewals_per_session'].append(current.renewals_completed)
            previous=current
            began=clock()
            async with asyncio.timeout(10):
                await previous.close(code=4000,message=b'idle endurance reconnect')
                current=await ready.get()
            if current.session_id==previous.session_id:
                raise RuntimeError('idle reconnect reused authorization')
            duration=round((clock()-began)*1000)
            state['reconnects'].append(dict(at_ms=round((began-started)*1000),duration_ms=duration))
            mark('idle_fresh_grant_reconnect',count=len(state['reconnects']),duration_ms=duration)
            next_reconnect+=interval
            # A fresh status after reconnection distinguishes live owner
            # diagnostics from serial text retained from the retired session.
            await pause(6)
            await snapshot()
        if clock()>=next_sample:
            await snapshot()
            next_sample=clock()+30
            mark('idle_soak_progress',idle_elapsed_ms=round((clock()-started)*1000),
                renewals=current.renewals_completed,reconnects=len(state['reconnects']))
        await pause(.2)
    check_session()
    await snapshot()
    state['elapsed_ms']=round((clock()-started)*1000)
    state['renewals_per_session'].append(current.renewals_completed)
    if (len(state['reconnects'])!=(seconds-1)//interval or any(n<1 for n in state['renewals_per_session'])
            or state['elapsed_ms']<seconds*1000):
        raise RuntimeError('idle duration, renewal or reconnect coverage incomplete')
    state['protocol_pass']=True
    mark('idle_soak_finished',idle_elapsed_ms=state['elapsed_ms'],reconnects=len(state['reconnects']))
    # Heap snapshots and RSS are retained for the separate cumulative recovery
    # audit. A successful protocol loop is not a leak-free endurance claim.
    return current


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('monitor',))
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,required=True)
    args=parser.parse_args()
    if not 1<=args.seconds<=32400:parser.error('monitor duration must be 1..32400 seconds')
    try:serial_monitor(args)
    except Exception as error:
        raise SystemExit('Idle monitor failed ('+type(error).__name__+'); inspect private serial log') from None


if __name__=='__main__':main()
