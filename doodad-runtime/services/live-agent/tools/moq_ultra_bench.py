#!/usr/bin/env python3
"""Enroll and test the physical full-shell Ultra against a private local host.

Requires the new firmware already installed. Default: no microphone/audio;
test real enrollment, WSS/QUIC readiness, forced reconnect and lease renewal.
--audio additionally captures microphone PCM (1.2 seconds by default), discards
it after counting, then plays a synthetic tone and checks the DMA receipt.
--capture-rounds repeats completed captures within one authenticated session.
No provider runs, deployed service changes, flash writes or restoration.
"""
import argparse
import asyncio
import json
import math
import os
from pathlib import Path
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


def pki(directory, host, fault=None):
    ca, leaf = ec.generate_private_key(ec.SECP256R1()), ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'VoiceWatch private hardware bench')])
    now = datetime.now(timezone.utc)
    def builder(public, subject, expired=False, future=False):
        return (x509.CertificateBuilder().subject_name(subject).issuer_name(name).public_key(public)
                .serial_number(x509.random_serial_number()).not_valid_before(now+timedelta(hours=1) if future else now-timedelta(hours=2))
                .not_valid_after(now-timedelta(hours=1) if expired else now+timedelta(hours=6)))
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
                  capture_rounds=args.capture_rounds if args.audio else 0, captures_completed=0)
    if args.certificate_fault:
        result['certificate_fault'] = args.certificate_fault
    began = time.monotonic()
    processes, streams = [], []
    server = None
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
        stdout, stderr = await asyncio.wait_for(process.communicate(), 15)
        write(directory/('usb-'+command+'.log'), stdout+stderr)
        if process.returncode:
            raise RuntimeError('USB ' + command + ' failed')
    try:
        await usb('info', 'device.json')
        device = json.loads((directory/'device.json').read_text())
        key = secrets.token_bytes(32)
        context, roots = pki(directory, args.host, args.certificate_fault)
        control_port, time_port, media_port = port(socket.SOCK_STREAM), port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
        write(directory/'devices.json', {device['device_id']: key.hex()})
        write(directory/'host.json', dict(certificate=str(directory/'server.pem'),
            private_key=str(directory/'server.key'), device_keys=str(directory/'devices.json'),
            ipc_socket=str(directory/'media.sock'), public_host=args.host,
            media_port=media_port, time_port=time_port))
        lease_seconds = max(45, math.ceil((args.capture_ms/1000+1)*args.capture_rounds+20)) if args.audio else 45
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
        capture_sample_base = 0
        async def on_audio(_, pcm):
            result['microphone_samples'] += len(pcm)//2
            if not args.audio:
                raise RuntimeError('unexpected microphone audio')
        async def on_event(_, kind, payload):
            if kind == 'connected':
                result['ready_sessions'] += 1
                ready.put_nowait(server.sessions[device['device_id']])
            if kind == 'capture.stopped':
                if int(payload['samples']) != result['microphone_samples']-capture_sample_base:
                    raise RuntimeError('capture sample count mismatch')
                captured.set()
            # No transcripts, personal app state, IDs or audio in public output.
            if kind in {'identified', 'connected', 'disconnected', 'capture.started', 'capture.stopped', 'capture.failed'}:
                mark(kind)
        server = MoqTransportServer(LatencyTrace(), on_audio, on_event, control_port,
            registry=registry, context=context, ipc_path=directory/'media.sock', media_host=args.host,
            media_port=media_port, time_port=time_port)
        await server.start()
        write(directory/'endpoint.json', dict(listen=f'0.0.0.0:{media_port}',
            certificate=str(directory/'server.pem'), private_key=str(directory/'server.key'),
            ipc_socket=str(directory/'media.sock')))
        native_log = (directory/'native.log').open('wb'); streams.append(native_log)
        native = await asyncio.create_subprocess_exec(str(ENDPOINT), '--config', str(directory/'endpoint.json'),
            stdout=native_log, stderr=asyncio.subprocess.STDOUT)
        processes.append(native)
        write(directory/'profile.json', dict(v=1, revision=device['revision']+1, device_id=device['device_id'],
            host=args.host, control_port=control_port, time_port=time_port, roots_pem=roots, key_hex=key.hex()))
        await usb('install', 'enrollment.json', '--profile', directory/'profile.json')
        monitor = await asyncio.create_subprocess_exec(str(args.idf_python), str(ENROLL), 'monitor',
            '--port', args.port, '--output', str(directory/'serial.log'), '--seconds', str(lease_seconds+120),
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
            return
        first = await asyncio.wait_for(ready.get(), 45)
        await asyncio.sleep(1)
        if result['microphone_samples']:
            raise RuntimeError('startup opened microphone')
        mark('idle_ready_without_capture')
        at = time.monotonic()
        await first.close(code=4000, message=b'bench reconnect')
        second = await asyncio.wait_for(ready.get(), 35)
        if first.session_id == second.session_id:
            raise RuntimeError('reconnect reused grant')
        result['forced_reconnect_ms'] = round((time.monotonic()-at)*1000)
        mark('fresh_grant_reconnect', duration_ms=result['forced_reconnect_ms'])
        if args.audio:
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
                mark('capture_round_completed', round=capture_round+1, samples=samples)
            # Deliberately non-frame-aligned tail, generated PCM only.
            samples = 16037
            tone = b''.join(struct.pack('<h', round(1800*math.sin(2*math.pi*440*n/16000))) for n in range(samples))
            second.begin_downlink(); second.enqueue_downlink(tone, 16000); second.end_downlink()
            await asyncio.wait_for(second.resume_after_downlink(), 12)
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
            await asyncio.wait_for(second.resume_after_downlink(), 12)
            if not interrupted.cancelled or interrupted.finished.is_set():
                raise RuntimeError('cancelled response reported successful completion')
            result['response_replacement_pass'] = True
            mark('cancelled_response_replaced_without_capture')
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
        if server:
            await server.close()
        for process in reversed(processes):
            await stop(process)
        for stream in streams:
            stream.close()
        result['elapsed_ms'] = round((time.monotonic()-began)*1000)
        write(directory/'result.json', result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', required=True)
    parser.add_argument('--host', required=True, help='LAN IPv4 address reachable from the Ultra')
    parser.add_argument('--idf-python', type=Path, required=True)
    parser.add_argument('--audio', action='store_true')
    parser.add_argument('--voice-ui', action='store_true', help='Show the real listening shell during capture')
    parser.add_argument('--capture-ms', type=int, default=1200)
    parser.add_argument('--capture-rounds', type=int, default=1)
    parser.add_argument('--certificate-fault', choices=('expired', 'not_yet_valid', 'hostname', 'untrusted'))
    args = parser.parse_args()
    if args.audio and args.certificate_fault:
        parser.error('certificate rejection tests never capture audio')
    if not 100 <= args.capture_ms <= 30000 or (args.voice_ui and not args.audio):
        parser.error('capture-ms must be 100..30000; voice-ui requires audio')
    if (not 1 <= args.capture_rounds <= 100 or (args.capture_rounds != 1 and not args.audio)
            or (args.capture_ms/1000+1)*args.capture_rounds+20 > 300):
        parser.error('capture-rounds requires audio, must be 1..100, and must fit a 300 second lease')
    ip_address(args.host)
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit('Physical bench failed ('+type(error).__name__+'); inspect private evidence') from None


if __name__ == '__main__':
    main()
