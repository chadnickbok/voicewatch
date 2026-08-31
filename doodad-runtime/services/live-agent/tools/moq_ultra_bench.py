#!/usr/bin/env python3
"""Enroll and test the physical full-shell Ultra against a private local host.

Requires the new firmware already installed. Default: no microphone/audio;
test real enrollment, WSS/QUIC readiness, forced reconnect and lease renewal.
--audio additionally captures microphone PCM (1.2 seconds by default), discards
it after counting, then plays a synthetic tone and checks the DMA receipt.
--capture-rounds repeats completed captures within one authenticated session.
--reply-each-capture plays a generated tone after every capture, exercising
repeated microphone-to-speaker transitions without providers or storing PCM.
--loss-percent/--added-rtt-ms exercise encrypted UDP loss and delay independently
of providers. WSS remains intact; a seeded proxy is closed with the bench.
--max-playout-pressure optionally fails on packet queue overflow. This is a
transport regression gate, not a calibrated speech-quality measurement.
--max-quic-heap-bytes requires numeric allocator snapshots within that budget,
with zero denials/system failures; it does not measure TLS or total device heap.
--capture-outage-ms injects one uplink blackout during an extra capture and
requires a loss-budget abort, then successful capture on the same session.
--long-response-seconds plays up to 600 seconds of generated, mostly quiet PCM
with a non-frame-aligned tail. It needs no microphone and requires same-session
renewal when playback exceeds the short bench lease. No provider is involved.
--stream-soak-groups exercises 50 synthetic Opus groups/second in each direction
using an explicitly selected test host and test firmware, without opening audio.
--idle-seconds runs the 120-second smoke or 28,800-second idle/reconnect gate;
its test firmware must support the read-only USB STATS command.
No provider runs, deployed service changes, flash writes or restoration.
"""
import argparse
import asyncio
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import ssl
import struct
import sys
import time
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

from doodad_agent.metrics import LatencyTrace
from doodad_agent.moq_auth import GrantRegistry
from doodad_agent.transport_moq import MoqTransportServer

ROOT = Path(__file__).resolve().parents[4]
ENROLL = ROOT / 'doodad-runtime/tools/moq_enroll.py'
ENDPOINT = ROOT / 'libs/moq-esp32/server/voice_agent/target/debug/voicewatch-moq-endpoint'


def write(path, value):
    raw = value if isinstance(value, bytes) else json.dumps(value, indent=2).encode() + b'\n'
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        stream.write(raw)


def pki(directory, host, fault=None, *, valid_for_hours=6):
    if type(valid_for_hours) is not int or not 1<=valid_for_hours<=24:
        raise ValueError('private bench certificate lifetime must be 1..24 hours')
    ca, leaf = ec.generate_private_key(ec.SECP256R1()), ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'VoiceWatch private hardware bench')])
    now = datetime.now(timezone.utc)
    def builder(public, subject, expired=False, future=False):
        return (x509.CertificateBuilder().subject_name(subject).issuer_name(name).public_key(public)
                .serial_number(x509.random_serial_number()).not_valid_before(now+timedelta(hours=1) if future else now-timedelta(hours=2))
                .not_valid_after(now-timedelta(hours=1) if expired else now+timedelta(hours=valid_for_hours)))
    root = (builder(ca.public_key(), name).add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
            .sign(ca, hashes.SHA256()))
    leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, host)])
    cert = (builder(leaf.public_key(), leaf_name, fault=='expired', fault=='not_yet_valid').add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ip_address('192.0.2.123' if fault=='hostname' else host))]), False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False)
            .sign(ca, hashes.SHA256()))
    pem = serialization.Encoding.PEM
    roots = root.public_bytes(pem)
    if fault == 'untrusted':
        other = ec.generate_private_key(ec.SECP256R1())
        roots = (builder(other.public_key(), name).add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
                 .sign(other, hashes.SHA256()).public_bytes(pem))
    write(directory/'roots.pem', roots)
    write(directory/'server.pem', cert.public_bytes(pem)+root.public_bytes(pem))
    write(directory/'server.key', leaf.private_bytes(pem, serialization.PrivateFormat.PKCS8,
                                                    serialization.NoEncryption()))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(directory/'server.pem', directory/'server.key')
    return context, roots.decode('ascii')


def port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('0.0.0.0', 0))
        return sock.getsockname()[1]


def shell_telemetry(raw):
    """Export only named numeric diagnostics, never arbitrary serial fields."""
    keys = ('frames','flushes','avg_render_us','max_render_us','avg_flush_us','max_flush_us',
            'touch_presses','internal_free','internal_min','internal_largest','psram_free')
    display = []
    for line in raw.splitlines():
        if '[display] fps=' not in line:
            continue
        values = dict(re.findall(r'\b([a-z_]+)=(\d+)\b', line))
        if all(key in values for key in keys):
            display.append({key:int(values[key]) for key in keys})
    tls = [dict(zip(('valid','live','peak','limit','blocks','allocations','frees','denied','failures'),map(int,values)))
           for values in re.findall(r'TLS heap valid=(\d+) live=(\d+) peak=(\d+) limit=(\d+) blocks=(\d+) allocations=(\d+) frees=(\d+) denied=(\d+) failures=(\d+)',raw)]
    return dict(display=display,tls_heap=tls,
                firmware_fault=any(marker in raw for marker in ('Guru Meditation','abort() was called','assert failed')))


async def stop(process):
    if process is not None and process.returncode is None:
        process.send_signal(signal.SIGINT)
        try:
            await asyncio.wait_for(process.wait(), 3)
        except TimeoutError:
            process.kill()
            await process.wait()


async def run(args):
    directory = args.output.resolve()
    directory.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.umask(0o077)
    result = dict(pass_=False, audio_requested=args.audio, microphone_samples=0, ready_sessions=0,
                  firmware_written=False, restoration_required=False,
                  voice_ui=args.voice_ui, capture_ms=args.capture_ms if args.audio else 0,
                  capture_rounds=args.capture_rounds if args.audio else 0, captures_completed=0,
                  reply_each_capture=args.reply_each_capture, round_trips_completed=0)
    if args.certificate_fault:
        result['certificate_fault'] = args.certificate_fault
    began = time.monotonic()
    processes, streams = [], []
    server = None
    monitor = None
    impairment = None
    memory_task = None
    sample_memory = None
    def mark(kind, **fields):
        event = dict(kind=kind, elapsed_ms=round((time.monotonic()-began)*1000), **fields)
        print(json.dumps(event), flush=True)
        with (directory/'events.jsonl').open('a') as output:
            output.write(json.dumps(event)+'\n')
    async def usb(command, output, *extra):
        process = await asyncio.create_subprocess_exec(str(args.idf_python), str(ENROLL), command,
            '--port', args.port, '--output', str(directory/output), *map(str, extra),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        processes.append(process)
        timeout = args.stream_soak_groups/50+45 if command == 'soak' else 15
        stdout, stderr = await asyncio.wait_for(process.communicate(), timeout)
        label = command+'-'+Path(output).name if command == 'monitor' else command
        write(directory/('usb-'+label+'.log'), stdout+stderr)
        if process.returncode:
            raise RuntimeError('USB ' + command + ' failed')
    try:
        if args.idle_seconds and sys.platform=='darwin':
            # Scoped to this bench process; no persistent power settings or
            # display wake lock. Cleanup also stops this owned helper.
            inhibitor=await asyncio.create_subprocess_exec('/usr/bin/caffeinate','-i','-w',str(os.getpid()),
                stdout=asyncio.subprocess.DEVNULL,stderr=asyncio.subprocess.DEVNULL)
            processes.append(inhibitor)
        await usb('info', 'device.json')
        device = json.loads((directory/'device.json').read_text())
        key = secrets.token_bytes(32)
        context, roots = pki(directory, args.host, args.certificate_fault,
                             valid_for_hours=10 if args.idle_seconds else 6)
        control_port, time_port, media_port = port(socket.SOCK_STREAM), port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
        backend_port = media_port
        if args.loss_percent or args.added_rtt_ms or args.capture_outage_ms:
            from moq_udp_impairment import UdpImpairment
            backend_port = port(socket.SOCK_DGRAM)
            impairment = UdpImpairment(args.loss_percent,args.added_rtt_ms,args.loss_seed)
            await impairment.start(('0.0.0.0',media_port),('127.0.0.1',backend_port))
        write(directory/'devices.json', {device['device_id']: key.hex()})
        write(directory/'host.json', dict(certificate=str(directory/'server.pem'),
            private_key=str(directory/'server.key'), device_keys=str(directory/'devices.json'),
            ipc_socket=str(directory/'media.sock'), public_host=args.host,
            media_port=media_port, time_port=time_port))
        lease_seconds = max(45, math.ceil((args.capture_ms/1000+1+2*args.reply_each_capture)*args.capture_rounds+20+10*bool(args.capture_outage_ms))) if args.audio else 45
        result['lease_seconds'] = lease_seconds
        registry = GrantRegistry({device['device_id']: key}, lease_seconds=lease_seconds)
        result['time_proofs_issued'] = 0
        issue_time = registry.time_proof
        def observed_time(device_id, nonce):
            proof = issue_time(device_id, nonce)
            result['time_proofs_issued'] += 1
            return proof
        registry.time_proof = observed_time
        ready, captured = asyncio.Queue(), asyncio.Event()
        capture_started, capture_failed = asyncio.Event(), asyncio.Event()
        recovery_started = None
        capture_sample_base = 0
        async def on_audio(_, pcm):
            result['microphone_samples'] += len(pcm)//2
            if not args.audio:
                raise RuntimeError('unexpected microphone audio')
            if recovery_started is not None and 'capture_recovery_ms' not in result:
                result['capture_recovery_ms'] = round((time.monotonic()-recovery_started)*1000)
        async def on_event(_, kind, payload):
            if kind == 'connected':
                result['ready_sessions'] += 1
                ready.put_nowait(server.sessions[device['device_id']])
            if kind == 'capture.stopped':
                if int(payload['samples']) != result['microphone_samples']-capture_sample_base:
                    raise RuntimeError('capture sample count mismatch')
                captured.set()
            if kind == 'capture.started': capture_started.set()
            if kind == 'capture.failed' and payload.get('reason') == 'loss_budget': capture_failed.set()
            # No transcripts, personal app state, IDs or audio in public output.
            if kind in {'identified', 'connected', 'disconnected', 'capture.started', 'capture.stopped', 'capture.failed'}:
                mark(kind)
        server = MoqTransportServer(LatencyTrace(directory/'latency.jsonl'), on_audio, on_event, control_port,
            registry=registry, context=context, ipc_path=directory/'media.sock', media_host=args.host,
            media_port=media_port, time_port=time_port)
        await server.start()
        endpoint_config = dict(listen=f'0.0.0.0:{backend_port}',
            certificate=str(directory/'server.pem'), private_key=str(directory/'server.key'),
            ipc_socket=str(directory/'media.sock'))
        if args.stream_soak_groups:
            endpoint_config['diagnostic_stream_soak_groups'] = args.stream_soak_groups
        write(directory/'endpoint.json', endpoint_config)
        native_log = (directory/'native.log').open('wb'); streams.append(native_log)
        native = await asyncio.create_subprocess_exec(str(args.endpoint or ENDPOINT), '--config', str(directory/'endpoint.json'),
            stdout=native_log, stderr=asyncio.subprocess.STDOUT)
        processes.append(native)
        if args.stream_soak_groups:
            result['native_memory'] = []
            async def sample_memory():
                probe = await asyncio.create_subprocess_exec('ps','-o','rss=','-p',str(native.pid),
                    stdout=asyncio.subprocess.PIPE,stderr=asyncio.subprocess.DEVNULL)
                try:
                    raw,_ = await asyncio.wait_for(probe.communicate(),2)
                finally:
                    if probe.returncode is None:
                        probe.kill()
                        await probe.wait()
                if probe.returncode or not raw.strip().isdigit() or len(result['native_memory'])>=128:
                    raise RuntimeError('native memory observation failed')
                result['native_memory'].append(dict(elapsed_ms=round((time.monotonic()-began)*1000),rss_kib=int(raw)))
            async def observe_memory():
                while True:
                    await sample_memory()
                    await asyncio.sleep(30)
            memory_task=asyncio.create_task(observe_memory())
        write(directory/'profile.json', dict(v=1, revision=device['revision']+1, device_id=device['device_id'],
            host=args.host, control_port=control_port, time_port=time_port, roots_pem=roots, key_hex=key.hex()))
        await usb('install', 'enrollment.json', '--profile', directory/'profile.json')
        if args.idle_seconds:
            monitor = await asyncio.create_subprocess_exec(str(args.idf_python),str(Path(__file__).with_name('moq_idle_soak.py')),
                'monitor','--port',args.port,'--output',str(directory/'serial.log'),
                '--seconds',str(args.idle_seconds+180),stdout=asyncio.subprocess.DEVNULL,stderr=asyncio.subprocess.DEVNULL)
            processes.append(monitor)
        elif not args.stream_soak_groups:
            monitor = await asyncio.create_subprocess_exec(str(args.idf_python), str(ENROLL), 'monitor',
                '--port', args.port, '--output', str(directory/'serial.log'), '--seconds', str(lease_seconds+120+args.long_response_seconds),
                stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
            processes.append(monitor)
        mark('enrolled')
        if args.certificate_fault:
            # The proof must succeed, then the real firmware HTTPS certificate
            # verifier must reject. Absence of a connection alone is not proof.
            async with asyncio.timeout(40):
                while True:
                    await asyncio.sleep(.25)
                    serial_path=directory/'serial.log'
                    raw=serial_path.read_text(errors='replace') if serial_path.exists() else ''
                    if not ready.empty() or result['microphone_samples']:
                        raise RuntimeError('invalid certificate was accepted')
                    if result['time_proofs_issued'] and 'moq_bootstrap: certificate rejected' in raw:
                        result['certificate_rejected'] = True
                        mark('certificate_rejected_after_time_proof')
                        break
            await asyncio.sleep(4)
            if not ready.empty() or server.sessions:
                raise RuntimeError('invalid certificate later established a session')
            if result['time_proofs_issued'] != 1:
                raise RuntimeError('terminal certificate failure was retried')
            result['pass_'] = True
            return result
        first = await asyncio.wait_for(ready.get(), 45)
        await asyncio.sleep(1)
        if result['microphone_samples']:
            raise RuntimeError('startup opened microphone')
        mark('idle_ready_without_capture')
        if args.idle_seconds:
            from moq_idle_soak import run_idle
            first=await run_idle(seconds=args.idle_seconds,server=server,first=first,ready=ready,
                device_id=device['device_id'],result=result,directory=directory,native=native,
                monitor=monitor,capture_started=capture_started,mark=mark)
        if args.stream_soak_groups:
            count = args.stream_soak_groups
            # One serial owner at a time. The soak command also records serial
            # telemetry; a simultaneous monitor could steal its acknowledgement.
            await usb('monitor', 'baseline-serial.log', '--seconds', 10)
            mark('stream_soak_started', groups=count, groups_per_second=50)
            await usb('soak', 'serial.log', '--groups', count, '--seconds', count//50+30)
            async with asyncio.timeout(5):
                while 'SOAK_HOST_FINAL ' not in (directory/'native.log').read_text(errors='replace'):
                    if native.returncode is not None:
                        raise RuntimeError('soak endpoint exited')
                    await asyncio.sleep(.1)
            raw = (directory/'serial.log').read_text(errors='replace')
            host = (directory/'native.log').read_text(errors='replace')
            finals = re.findall(r'SOAK_FINAL pass=(\d+) result=(-?\d+) sent=(\d+) received=(\d+) would_block=(\d+) microphone=(\d+) speaker=(\d+)', raw)
            peer = re.findall(r'SOAK_HOST_FINAL pass=(\d+) sent=(\d+) received=(\d+) elapsed_ms=(\d+)', host)
            progress = [dict(zip(('sent','received','next','elapsed_ms','internal_free','internal_min',
                'internal_largest','psram_free','rx_high','tx_high','leased'),map(int,values))) for values in re.findall(
                r'SOAK_PROGRESS sent=(\d+) received=(\d+) next=(\d+) elapsed_ms=(\d+) internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) psram_free=(\d+) rx_high=(\d+) tx_high=(\d+) leased=(\d+)',raw)]
            if len(finals)!=1 or len(peer)!=1 or not progress:
                raise RuntimeError('missing or ambiguous soak diagnostics')
            passed,code,sent,received,would_block,microphone,speaker = map(int,finals[0])
            peer_pass,peer_sent,peer_received,peer_elapsed = map(int,peer[0])
            if (not passed or code or (sent,received,peer_sent,peer_received)!=(count,)*4
                    or not peer_pass or microphone or speaker or progress[-1]['leased']
                    or progress[-1]['next']!=count+1
                    or min(peer_elapsed,progress[-1]['elapsed_ms'])<count*20):
                raise RuntimeError('soak integrity or duration failed')
            if (first._closed or first._fault.is_set() or not first.connected.is_set()
                    or server.sessions.get(device['device_id']) is not first or not ready.empty()
                    or result['ready_sessions']!=1 or result['microphone_samples'] or capture_started.is_set()):
                raise RuntimeError('soak changed session or opened capture')
            if count/50>=lease_seconds and first.renewals_completed<1:
                raise RuntimeError('soak did not renew its lease')
            result['stream_soak'] = dict(groups_per_direction=count,groups_per_second=50,
                watch_sent=sent,watch_received=received,host_sent=peer_sent,host_received=peer_received,
                host_elapsed_ms=peer_elapsed,watch_elapsed_ms=progress[-1]['elapsed_ms'],
                would_block=would_block,renewals=first.renewals_completed,same_session=True,
                microphone_opened=False,speaker_opened=False,progress=progress)
            mark('stream_soak_finished',groups=count,renewals=first.renewals_completed)
        at = time.monotonic()
        await first.close(code=4000, message=b'bench reconnect')
        second = await asyncio.wait_for(ready.get(), 35)
        if first.session_id == second.session_id:
            raise RuntimeError('reconnect reused grant')
        result['forced_reconnect_ms'] = round((time.monotonic()-at)*1000)
        mark('fresh_grant_reconnect', duration_ms=result['forced_reconnect_ms'])
        if args.stream_soak_groups:
            await usb('monitor', 'reconnect-serial.log', '--seconds', 10)
            if (second._closed or second._fault.is_set() or not second.connected.is_set()
                    or result['ready_sessions']!=2 or result['microphone_samples'] or capture_started.is_set()
                    or server.bridge.unexpected_failures or server.bootstrap.unexpected_failures
                    or native.returncode is not None):
                raise RuntimeError('post-soak recovery failed')
            result['pass_'] = True
            return result
        if args.audio:
            # Generated output only; microphone samples are counted/discarded.
            samples = 16037
            tone = b''.join(struct.pack('<h', round(1800*math.sin(2*math.pi*440*n/16000))) for n in range(samples))
            async def play_tone():
                second.begin_downlink(); second.enqueue_downlink(tone,16000); second.end_downlink()
                if not await asyncio.wait_for(second.resume_after_downlink(),12):
                    raise RuntimeError('synthetic playback was cancelled or replaced')
            if args.capture_outage_ms:
                await second.start_capture(3000)
                await asyncio.wait_for(capture_started.wait(),3)
                await asyncio.sleep(.3)
                impairment.blackout('uplink',args.capture_outage_ms)
                restoration = time.monotonic()+args.capture_outage_ms/1000
                mark('capture_outage_started',duration_ms=args.capture_outage_ms)
                await asyncio.wait_for(capture_failed.wait(),6)
                if captured.is_set():
                    raise RuntimeError('failed capture committed partial audio')
                await asyncio.sleep(max(0,restoration-time.monotonic()))
                if (server.sessions.get(device['device_id']) is not second or second._closed
                        or second._fault.is_set() or not second.connected.is_set()):
                    raise RuntimeError('capture loss retired the session')
                result['capture_loss_aborted']=True
                result['capture_outage_ms']=args.capture_outage_ms
                recovery_started=restoration
                mark('capture_loss_aborted_session_preserved')
            for capture_round in range(args.capture_rounds):
                capture_sample_base = result['microphone_samples']
                captured.clear()
                if args.voice_ui:
                    await second.send('agent.state', dict(schema_version=1, device_id=device['device_id'],
                        voice_phase='listening', display=dict(transcript='', response=''),
                        background=dict(running_count=0, focused_question=False, review_ready=False,
                            completion_pending=False, status_changed=False, install_state=0, tasks=[])))
                    mark('listening_ui_requested')
                await second.start_capture(args.capture_ms)
                await asyncio.wait_for(captured.wait(), args.capture_ms/1000+10)
                samples = result['microphone_samples']-capture_sample_base
                # The board delivers 10 ms chunks; requested duration rounds up.
                if samples != ((max(1000, args.capture_ms)+9)//10)*160:
                    raise RuntimeError('incomplete microphone capture')
                result['captures_completed'] += 1
                if args.capture_outage_ms and (server.sessions.get(device['device_id']) is not second
                        or result.get('capture_recovery_ms',10001)>10000):
                    raise RuntimeError('capture recovery exceeded same-session ten-second gate')
                mark('capture_round_completed', round=capture_round+1, samples=samples)
                if args.reply_each_capture:
                    await play_tone()
                    result['round_trips_completed'] += 1
                    mark('capture_reply_completed', round=capture_round+1, speaker_samples=16037)
            # Deliberately non-frame-aligned tail, generated PCM only.
            samples = 16037
            if not args.reply_each_capture:
                await play_tone()
            result['speaker_samples'] = samples
            mark('physical_audio_finished', microphone_samples=result['microphone_samples'], speaker_samples=samples)
            # Replace speech on the same completed capture. A response-only
            # cancellation must not erase that context or open the microphone.
            second.begin_downlink(); second.enqueue_downlink(tone*3, 16000); second.end_downlink()
            interrupted = second._response
            await asyncio.wait_for(interrupted.bound.wait(), 5)
            await asyncio.sleep(.2)
            second.clear_downlink()
            second.begin_downlink(); second.enqueue_downlink(tone, 16000); second.end_downlink()
            if not await asyncio.wait_for(second.resume_after_downlink(), 12):
                raise RuntimeError('replacement playback was cancelled or replaced')
            if not interrupted.cancelled or interrupted.finished.is_set():
                raise RuntimeError('cancelled response reported successful completion')
            result['response_replacement_pass'] = True
            mark('cancelled_response_replaced_without_capture')
        if args.long_response_seconds:
            if not args.audio:
                await second.authorize_response('text')
            # Observe read completion without storing PCM or writing per packet.
            # Preserve the real spool/pacer and its generation/cancellation path.
            read_pcm=second.downlink.read
            pacing=dict(packets=0,max_interval_ms=0.0,elapsed_ms=0.0,late_intervals=[])
            first_pcm=last_pcm=None
            async def observed_read(generation):
                nonlocal first_pcm,last_pcm
                packet=await read_pcm(generation)
                if packet is not None:
                    now=time.monotonic()
                    if first_pcm is None: first_pcm=now
                    if last_pcm is not None:
                        interval=(now-last_pcm)*1000
                        pacing['max_interval_ms']=max(pacing['max_interval_ms'],round(interval,3))
                        if interval>=30 and len(pacing['late_intervals'])<8:
                            pacing['late_intervals'].append(dict(at_ms=round((now-first_pcm)*1000,3),interval_ms=round(interval,3)))
                    last_pcm=now
                    pacing['packets']+=1
                    pacing['elapsed_ms']=round((now-first_pcm)*1000,3)
                return packet
            second.downlink.read=observed_read
            result['long_pacing']=pacing
            # A quiet 100 ms marker followed by 900 ms intentional silence.
            # Feed the normal bounded host spool; never allocate the full PCM.
            marker = b''.join(struct.pack('<h',round(300*math.sin(2*math.pi*500*n/16000))) for n in range(1600))
            block = marker + bytes(14400*2)
            count = args.long_response_seconds*16000-123
            second.begin_downlink()
            response = second._response
            for offset in range(0,count,16000):
                second.enqueue_downlink(block[:min(16000,count-offset)*2],16000)
            second.end_downlink()
            mark('long_playback_started',samples=count,seconds=args.long_response_seconds)
            began_playback=time.monotonic()
            completion=asyncio.create_task(second.resume_after_downlink())
            processes_at_start=result['ready_sessions']
            try:
                async with asyncio.timeout(args.long_response_seconds+20):
                    while not completion.done():
                        await asyncio.wait({completion},timeout=min(15,args.long_response_seconds))
                        mark('long_playback_progress',renewals=second.renewals_completed,
                             playback_elapsed_ms=round((time.monotonic()-began_playback)*1000))
                    if not await completion:
                        raise RuntimeError('long playback cancelled')
            finally:
                if not completion.done(): completion.cancel()
                await asyncio.gather(completion,return_exceptions=True)
            if (server.sessions.get(device['device_id']) is not second or second._response is not response
                    or result['ready_sessions']!=processes_at_start or response.samples!=count
                    or not response.finished.is_set() or response.cancelled):
                raise RuntimeError('long playback lost response/session ownership')
            if args.long_response_seconds>=lease_seconds and second.renewals_completed<1:
                raise RuntimeError('long playback did not cross a renewed lease')
            result['long_response']=dict(samples=count,seconds=args.long_response_seconds,
                playback_elapsed_ms=round((time.monotonic()-began_playback)*1000),
                renewals=second.renewals_completed,exact_speaker_receipt=True,same_session=True)
            mark('long_playback_finished',**result['long_response'])
            # Completion can reach WSS before the serial monitor drains the
            # endpoint's final timing/allocator snapshot. Require that snapshot.
            async with asyncio.timeout(3):
                marker=f'playout diagnostics complete samples={count}'
                while marker not in (directory/'serial.log').read_text(errors='replace'):
                    await asyncio.sleep(.1)
            result['long_final_diagnostics']=True
        if second._renewal_supported:
            async with asyncio.timeout(lease_seconds+15):
                while second.renewals_completed<2:
                    if second._closed or second._fault.is_set(): raise RuntimeError('renewal session retired')
                    await asyncio.sleep(.1)
            if not ready.empty() or server.sessions.get(device['device_id']) is not second:
                raise RuntimeError('renewal replaced session')
            result['renewals_completed']=second.renewals_completed
            mark('same_session_renewal_verified',count=second.renewals_completed)
        else:
            third = await asyncio.wait_for(ready.get(), lease_seconds+15)
            if third.session_id in {first.session_id, second.session_id}:
                raise RuntimeError('lease renewal reused grant')
            mark('lease_renewed_with_fresh_grant')
        if server.bridge.unexpected_failures or server.bootstrap.unexpected_failures:
            raise RuntimeError('host boundary unexpected failures')
        if native.returncode is not None:
            raise RuntimeError('native endpoint exited')
        result['pass_'] = True
    except Exception as error:
        result['failure_type'] = type(error).__name__
        mark('failed', failure_type=type(error).__name__)
        raise
    finally:
        if args.idle_seconds:
            result['idle_monitor_survived']=monitor is not None and monitor.returncode is None
            result['pass_']=result['pass_'] and result['idle_monitor_survived']
        if memory_task is not None:
            memory_task.cancel()
            observed = await asyncio.gather(memory_task,return_exceptions=True)
            result['native_memory_observation_failed'] = any(
                isinstance(value,BaseException) and not isinstance(value,asyncio.CancelledError) for value in observed)
            try:
                await sample_memory()
            except Exception:
                result['native_memory_observation_failed'] = True
            result['pass_']=result['pass_'] and not result['native_memory_observation_failed']
        if server:
            await server.close()
        for process in reversed(processes):
            await stop(process)
        for stream in streams:
            stream.close()
        if impairment:
            impairment.close()
            result['impairment']=impairment.snapshot()
            if any(item['pressure'] for item in impairment.stats.values()):
                result['pass_']=False
        serial_path=directory/'serial.log'
        raw=serial_path.read_text(errors='replace') if serial_path.exists() else ''
        if args.idle_seconds:
            observed=shell_telemetry(raw)
            result['idle_tls_heap']=observed['tls_heap']
            result['idle_tls_gate']=bool(observed['tls_heap']) and all(
                h['valid']==1 and 0<h['limit']<=262144 and h['live']<=h['peak']<=h['limit']
                and not h['denied'] and not h['failures'] for h in observed['tls_heap'])
            result['pass_']=result['pass_'] and result['idle_tls_gate'] and not observed['firmware_fault']
        if args.stream_soak_groups:
            result['shell_telemetry'] = {}
            for label,filename in (('baseline','baseline-serial.log'),('soak','serial.log'),('reconnected','reconnect-serial.log')):
                path=directory/filename
                result['shell_telemetry'][label]=shell_telemetry(path.read_text(errors='replace') if path.exists() else '')
            samples=result['shell_telemetry']['soak']['tls_heap']
            result['soak_tls_gate']=bool(samples) and all(
                h['valid']==1 and 0<h['limit']<=262144 and h['live']<=h['peak']<=h['limit']
                and not h['denied'] and not h['failures'] for h in samples)
            result['pass_']=result['pass_'] and result['soak_tls_gate'] and not any(
                t['firmware_fault'] for t in result['shell_telemetry'].values())
        heap_fields=('live','peak','limit','blocks','allocations','frees','denied','failures')
        result['quic_heap']=[dict(zip(heap_fields,map(int,values))) for values in re.findall(
            r'QUIC heap live=(\d+) peak=(\d+) limit=(\d+) blocks=(\d+) allocations=(\d+) frees=(\d+) denied=(\d+) failures=(\d+)',raw)]
        if args.max_quic_heap_bytes is not None:
            result['max_quic_heap_bytes']=args.max_quic_heap_bytes
            result['quic_heap_budget_gate']=bool(result['quic_heap']) and all(
                0<h['limit']<=args.max_quic_heap_bytes and h['live']<=h['peak']<=h['limit']
                and not h['denied'] and not h['failures'] for h in result['quic_heap'])
            result['pass_']=result['pass_'] and result['quic_heap_budget_gate']
        if args.audio or args.long_response_seconds:
            result['playout']=[dict(samples=int(samples),concealed=int(concealed),late=int(late),
                pressure=int(pressure),silence=int(silence) if silence else None)
                for samples,concealed,late,pressure,silence in re.findall(
                    r'playout samples=(\d+) concealed=(\d+) late=(\d+) pressure=(\d+)(?: silence=(\d+))?',raw)]
            if args.max_playout_pressure is not None:
                result['max_playout_pressure']=args.max_playout_pressure
                expected=(args.capture_rounds if args.reply_each_capture else 1)+1
                result['playout_pressure_gate']=(len(result['playout'])==expected and
                    all(p['samples']==16037 and p['pressure']<=args.max_playout_pressure for p in result['playout']))
                result['pass_']=result['pass_'] and result['playout_pressure_gate']
        result['elapsed_ms'] = round((time.monotonic()-began)*1000)
        if args.long_response_seconds:
            expected=args.long_response_seconds*16000-123
            matches=[p for p in result.get('playout',[]) if p['samples']==expected]
            result['long_playout_gate']=len(matches)==1 and all(
                not p['concealed'] and not p['late'] and not p['pressure'] and p['silence']==0 for p in matches)
            result['pass_']=result['pass_'] and result['long_playout_gate']
            result['pass_']=result['pass_'] and result.get('long_final_diagnostics',False)
        write(directory/'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', required=True)
    parser.add_argument('--host', required=True, help='LAN IPv4 address reachable from the Ultra')
    parser.add_argument('--idf-python', type=Path, required=True)
    parser.add_argument('--endpoint', type=Path, help='Explicit native host binary; deployed services are unchanged')
    parser.add_argument('--stream-soak-groups', type=int, choices=(0,500,3000,90000), default=0)
    parser.add_argument('--idle-seconds',type=int,choices=(0,120,28800),default=0)
    parser.add_argument('--audio', action='store_true')
    parser.add_argument('--long-response-seconds',type=int,default=0)
    parser.add_argument('--voice-ui', action='store_true', help='Show the real listening shell during capture')
    parser.add_argument('--capture-ms', type=int, default=1200)
    parser.add_argument('--capture-rounds', type=int, default=1)
    parser.add_argument('--reply-each-capture',action='store_true',
                        help='Play a generated response after every capture, retaining no microphone PCM')
    parser.add_argument('--capture-outage-ms',type=int,default=0,
                        help='Require bounded capture failure and same-session recovery after an uplink blackout')
    parser.add_argument('--loss-percent',type=int,choices=(0,1,3,5),default=0)
    parser.add_argument('--added-rtt-ms',type=int,choices=(0,30,60,120),default=0)
    parser.add_argument('--loss-seed',type=int,default=44)
    parser.add_argument('--max-playout-pressure',type=int,
                        help='Require all completed tones to have pressure no greater than this count')
    parser.add_argument('--max-quic-heap-bytes',type=int,
                        help='Require allocator snapshots, this maximum budget, and zero allocation failures')
    parser.add_argument('--certificate-fault', choices=('expired', 'not_yet_valid', 'hostname', 'untrusted'))
    args = parser.parse_args()
    if args.idle_seconds and (args.audio or args.stream_soak_groups or args.long_response_seconds
            or args.certificate_fault or args.loss_percent or args.added_rtt_ms or args.capture_outage_ms):
        parser.error('idle-seconds cannot combine with audio, stream load, certificate faults or impairment')
    if args.stream_soak_groups and (args.endpoint is None or args.audio or args.long_response_seconds
            or args.certificate_fault or args.loss_percent or args.added_rtt_ms or args.capture_outage_ms):
        parser.error('stream-soak-groups requires an explicit test endpoint and no audio, certificate fault or impairment mode')
    if not 0<=args.long_response_seconds<=600 or (args.long_response_seconds and
            (args.certificate_fault or args.max_playout_pressure is not None)):
        parser.error('long-response-seconds must be 0..600 and cannot combine with certificate faults or the short-tone gate')
    if args.audio and args.certificate_fault:
        parser.error('certificate rejection tests never capture audio')
    if args.reply_each_capture and not args.audio:
        parser.error('reply-each-capture requires audio')
    if args.capture_outage_ms and (not args.audio or not 1 <= args.capture_outage_ms <= 2000):
        parser.error('capture-outage-ms requires audio and a duration from 1 to 2000')
    if args.max_playout_pressure is not None and (not args.audio or args.max_playout_pressure<0):
        parser.error('max-playout-pressure requires audio and a nonnegative limit')
    if args.max_quic_heap_bytes is not None and not 1<=args.max_quic_heap_bytes<=1024*1024:
        parser.error('max-quic-heap-bytes must be 1..1048576')
    if not 100 <= args.capture_ms <= 30000 or (args.voice_ui and not args.audio):
        parser.error('capture-ms must be 100..30000; voice-ui requires audio')
    if (not 1 <= args.capture_rounds <= 100 or (args.capture_rounds != 1 and not args.audio)
            or (args.capture_ms/1000+1+2*args.reply_each_capture)*args.capture_rounds+20+10*bool(args.capture_outage_ms) > 300):
        parser.error('capture-rounds requires audio, must be 1..100, and must fit a 300 second lease')
    ip_address(args.host)
    try:
        result=asyncio.run(run(args))
        if not result['pass_']:
            raise RuntimeError('physical bench acceptance gate failed')
    except Exception as error:
        raise SystemExit('Physical bench failed ('+type(error).__name__+'); inspect private evidence') from None


if __name__ == '__main__':
    main()
