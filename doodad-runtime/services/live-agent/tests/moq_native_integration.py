"""Explicit native lane: build server/voice_agent --bins --examples first.

Uses real HTTPS/WSS issuance, private Unix IPC and native QUIC with synthetic
PCM. The fixture supplies application control; it is not the production agent
adapter or a physical watch/VoiceOrb acceptance test.
"""
from __future__ import annotations
import asyncio
import json
import signal
import socket
import ssl
import tempfile
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
from pathlib import Path

import pytest
from aiohttp import ClientSession
from aiohttp.test_utils import TestServer
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

from doodad_agent.moq_auth import GrantRegistry, bootstrap_proof
from doodad_agent.moq_bootstrap import MoqBootstrap
from doodad_agent.moq_bridge import MoqBridgeServer
from doodad_agent.metrics import LatencyTrace
from doodad_agent.transport_moq import MoqTransportServer

ROOT = Path(__file__).resolve().parents[4]
BIN = ROOT/'libs/moq-esp32/server/voice_agent/target/debug'
DEVICE = 'watch-ultra-native-test'
KEY = bytes(range(32))  # Public fixture only; never an enrolled device key.
IDENTITY = dict(capture_id='71', request_id='72', owner_token='73')


def write_private(path, data):
    path.write_bytes(data)
    path.chmod(0o600)


def trust(directory):
    root_key = ec.generate_private_key(ec.SECP256R1())
    leaf_key = ec.generate_private_key(ec.SECP256R1())
    root_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Native test root')])
    leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')])
    now = datetime.now(timezone.utc)
    def builder(subject, issuer, key):
        return (x509.CertificateBuilder().subject_name(subject).issuer_name(issuer)
                .public_key(key.public_key()).serial_number(x509.random_serial_number())
                .not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(hours=1)))
    root = (builder(root_name, root_name, root_key)
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .sign(root_key, hashes.SHA256()))
    leaf = (builder(leaf_name, root_name, leaf_key)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost'), x509.IPAddress(ip_address('127.0.0.1'))]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .sign(root_key, hashes.SHA256()))
    pem = serialization.Encoding.PEM
    root_file, cert_file, key_file = (directory/name for name in ('root.pem', 'server.pem', 'server.key'))
    write_private(root_file, root.public_bytes(pem))
    write_private(cert_file, leaf.public_bytes(pem)+root.public_bytes(pem))
    write_private(key_file, leaf_key.private_bytes(pem, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server.load_cert_chain(cert_file, key_file)
    return server, ssl.create_default_context(cafile=str(root_file)), root_file, cert_file, key_file


async def stop(process):
    if process is not None and process.returncode is None:
        process.send_signal(signal.SIGINT)
        try:
            await asyncio.wait_for(process.wait(), 3)
        except TimeoutError:
            process.kill()
            await process.wait()


async def probe(config, directory):
    path = directory/'probe.json'
    write_private(path, json.dumps(config).encode())
    process = await asyncio.create_subprocess_exec(str(BIN/'examples/native_probe'), str(path),
                                                  stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    return process


@pytest.mark.asyncio
@pytest.mark.parametrize('mode', ['audio', 'bad_token', 'revoked'])
async def test_native_authorized_session(mode):
    assert (BIN/'voicewatch-moq-endpoint').is_file(), 'Build the native endpoint before running this lane'
    with tempfile.TemporaryDirectory(prefix='vw-native-', dir='/tmp') as temporary:
        directory = Path(temporary)
        registry = GrantRegistry({DEVICE: KEY})
        controls, attached, packets = [], [], []
        ready, encoded, closed = asyncio.Event(), asyncio.Event(), asyncio.Event()
        capture = bytearray()
        async def control(ws, sid, owner):
            controls.append((sid, owner))
            async for _ in ws: pass
        async def attach(peer): attached.append(peer)
        async def received(peer, packet):
            h = packet.header
            packets.append(h['type'])
            if h['type'] == 'media.ready':
                ready.set()
                if mode != 'audio': return
                await peer.send('capture.begin', {**IDENTITY, 'first_group': '0'})
                # Deliberately race the authenticated end ahead of media. The
                # worker must still wait for and validate the complete tail.
                await peer.send('capture.end', {**IDENTITY, 'end_group': '3', 'samples': '537'})
            elif h['type'] == 'capture.pcm':
                assert all(h[key] == value for key, value in IDENTITY.items())
                capture.extend(packet.pcm)
                assert len(capture) <= 1074
            elif h['type'] == 'capture.ended':
                assert h['samples'] == '537' and h['end_group'] == '3' and len(capture) == 1074
                await peer.send('playback.begin', {**IDENTITY, 'response_id': '1'})
            elif h['type'] == 'playback.prepared':
                assert h['first_group'] == '0'
                # Test-only controlled binding: the reference probe subscribes
                # to response audio before it sends any microphone samples.
                await peer.send('playback.bound', {**IDENTITY, 'response_id': '1'})
                for offset in range(0, len(capture), 640):
                    await peer.send('playback.pcm', {**IDENTITY, 'response_id': '1'}, bytes(capture[offset:offset+640]))
                    await asyncio.sleep(0.02)
                await peer.send('playback.end', {**IDENTITY, 'response_id': '1'})
            elif h['type'] == 'playback.encoded':
                assert h['samples'] == '537' and h['end_group'] == '3'
                encoded.set()
            elif h['type'] == 'ping':
                await peer.send('pong')
        async def close(peer): closed.set()
        bridge = MoqBridgeServer(directory/'media.sock', registry, attach, received, close)
        await bridge.start()
        server_context, client_context, roots, cert, key = trust(directory)
        port_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        port_socket.bind(('127.0.0.1', 0)); port = port_socket.getsockname()[1]; port_socket.close()
        bootstrap = MoqBootstrap(registry, control, media_host='127.0.0.1', media_port=port)
        web_server = TestServer(bootstrap.application(), scheme='https')
        await web_server.start_server(ssl=server_context)
        native_config = directory/'endpoint.json'
        write_private(native_config, json.dumps(dict(listen=f'127.0.0.1:{port}', certificate=str(cert),
                                                     private_key=str(key), ipc_socket=str(bridge.path))).encode())
        endpoint = child = None
        try:
            endpoint = await asyncio.create_subprocess_exec(str(BIN/'voicewatch-moq-endpoint'), '--config', str(native_config),
                                                            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            line = await asyncio.wait_for(endpoint.stderr.readline(), 5)
            assert b'listening' in line, line
            async with ClientSession() as client:
                async with client.post(web_server.make_url('/v1/moq/challenge'), json={'device_id': DEVICE}, ssl=client_context) as response:
                    assert response.status == 200
                    nonce = (await response.json())['challenge']
                body = dict(device_id=DEVICE, challenge=nonce, proof=bootstrap_proof(KEY, DEVICE, nonce))
                async with client.post(web_server.make_url('/v1/moq/bootstrap'), json=body, ssl=client_context) as response:
                    assert response.status == 200
                    grant = await response.json()
                ws = await client.ws_connect(web_server.make_url('/v1/moq/control'), ssl=client_context,
                                             headers={'Authorization': 'Bearer '+grant['control_token']})
                configuration = dict(address=f'127.0.0.1:{port}', roots=str(roots), setup_path=grant['setup_path'],
                                     publish=grant['publish'], subscribe=grant['subscribe'], mode='audio')
                if mode == 'bad_token':
                    configuration.update(mode='reject', setup_path='/voicewatch/v1?token='+'B'*43)
                elif mode == 'revoked': configuration['mode'] = 'close'
                child = await probe(configuration, directory)
                if mode == 'revoked':
                    await asyncio.wait_for(ready.wait(), 5)
                    await ws.close()
                stdout, stderr = await asyncio.wait_for(child.communicate(), 15)
                assert child.returncode == 0, (stderr.decode(), packets, len(capture), bridge.unexpected_failures)
                result = json.loads(stdout)
                if mode == 'audio':
                    assert result['reference_pcm_match'] and result['input_samples'] == result['output_samples'] == 537
                    await asyncio.wait_for(encoded.wait(), 1)
                    await asyncio.wait_for(closed.wait(), 2)
                    assert len(attached) == 1 and len(capture) == 1074
                    configuration['mode'] = 'reject'  # Same media token cannot reconnect.
                    child = await probe(configuration, directory)
                    stdout, stderr = await asyncio.wait_for(child.communicate(), 6)
                    assert child.returncode == 0 and json.loads(stdout)['rejected'], stderr.decode()
                elif mode == 'bad_token':
                    assert result['rejected'] and not attached and not packets
                else:
                    assert result['closed'] and not capture
                    assert not registry.valid(controls[0][0], controls[0][1])
                await ws.close()
                assert not bridge.unexpected_failures
        finally:
            await stop(child)
            await stop(endpoint)
            await web_server.close()
            await bridge.close()


@pytest.mark.asyncio
@pytest.mark.parametrize('cancel_before_bound', [False, True])
async def test_product_session_over_real_native_boundary(cancel_before_bound):
    """Real product adapter, synthetic application PCM, emulated watch receipts.

    Unlike the framing fixture above, no test callback writes native IPC. This
    exercises the production session's capture/response ownership and queues.
    It still does not assert a physical DMA receipt or a live provider turn.
    """
    with tempfile.TemporaryDirectory(prefix='vw-session-', dir='/tmp') as temporary:
        directory = Path(temporary)
        registry = GrantRegistry({DEVICE: KEY})
        server_context, client_context, roots, cert, key = trust(directory)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]; sock.close()
        captured = bytearray()
        ready, played = asyncio.Event(), asyncio.Event()
        waits = []
        events = []

        async def audio(device, pcm):
            assert device == DEVICE
            captured.extend(pcm)
            assert len(captured) <= 1074

        def respond(session, pcm, *, finish=True):
            session.begin_downlink()
            session.enqueue_downlink(pcm, 16000)
            if finish:
                session.end_downlink()

        async def event(device, kind, payload):
            events.append(kind)
            if kind == 'connected':
                ready.set()
            elif kind == 'listen.requested':
                await transport.sessions[device].start_capture()
            elif kind == 'capture.stopped':
                assert payload['samples'] == '537' and len(captured) == 1074
                session = transport.sessions[device]
                respond(session, b'\x09\x00'*17 if cancel_before_bound else bytes(captured))
                if not cancel_before_bound:
                    async def wait():
                        await session.resume_after_downlink()
                        played.set()
                    waits.append(asyncio.create_task(wait()))

        transport = MoqTransportServer(LatencyTrace(), audio, event, 0, registry=registry,
                    context=server_context, ipc_path=directory/'media.sock',
                    media_host='127.0.0.1', media_port=port, host='127.0.0.1')
        await transport.start()
        # Port zero is only for the local test listener.
        address = transport.bootstrap._runner.addresses[0]
        base = f'https://127.0.0.1:{address[1]}'
        config = directory/'endpoint.json'
        write_private(config, json.dumps(dict(listen=f'127.0.0.1:{port}', certificate=str(cert),
                                              private_key=str(key), ipc_socket=str(transport.bridge.path))).encode())
        endpoint = child = None
        try:
            endpoint = await asyncio.create_subprocess_exec(str(BIN/'voicewatch-moq-endpoint'), '--config', str(config),
                                             stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
            assert b'listening' in await asyncio.wait_for(endpoint.stderr.readline(), 5)
            async with ClientSession() as client:
                async with client.post(base+'/v1/moq/challenge', json={'device_id': DEVICE}, ssl=client_context) as response:
                    nonce = (await response.json())['challenge']
                async with client.post(base+'/v1/moq/bootstrap', json=dict(device_id=DEVICE, challenge=nonce,
                                proof=bootstrap_proof(KEY, DEVICE, nonce)), ssl=client_context) as response:
                    grant = await response.json()
                ws = await client.ws_connect(base+'/v1/moq/control', ssl=client_context,
                                              headers={'Authorization': 'Bearer '+grant['control_token']})
                seq = 0
                async def send(kind, payload):
                    nonlocal seq
                    seq += 1
                    await ws.send_json(dict(v=1, seq=seq, type=kind, device_id=DEVICE,
                                            session_id=grant['session_id'], payload=payload))
                await send('hello', dict(device_id=DEVICE, board='t-watch-ultra', transport='moq', capabilities={}))
                welcome = await ws.receive_json(timeout=3)
                assert welcome['type'] == 'welcome'
                await send('peer.ready', {})
                child = await probe(dict(address=f'127.0.0.1:{port}', roots=str(roots), setup_path=grant['setup_path'],
                                          publish=grant['publish'], subscribe=grant['subscribe'], mode='audio'), directory)
                await asyncio.wait_for(ready.wait(), 5)
                await send('listen.requested', {})
                start = await ws.receive_json(timeout=3)
                assert start['type'] == 'capture.start'
                await send('capture.started', {**IDENTITY, 'first_group': '0', 'start_id': start['payload']['start_id']})
                await send('capture.stopped', {**IDENTITY, 'first_group': '0', 'end_group': '3', 'samples': '537'})
                while not played.is_set():
                    document = await ws.receive_json(timeout=5)
                    payload = document['payload']
                    assert document['session_id'] == grant['session_id'] and document['device_id'] == DEVICE
                    if document['type'] == 'playback.begin':
                        if cancel_before_bound and payload['response_id'] == '1':
                            session = transport.sessions[DEVICE]
                            session.clear_downlink()
                            respond(session, bytes(captured))
                            async def wait():
                                await session.resume_after_downlink()
                                played.set()
                            waits.append(asyncio.create_task(wait()))
                            # Delayed old bound receipt must not activate the
                            # replacement response or kill this connection.
                            await send('playback.bound', {**payload, 'samples': '0', 'cancelled': False, 'error': 0})
                        else:
                            await send('playback.bound', {**payload, 'samples': '0', 'cancelled': False, 'error': 0})
                    elif document['type'] == 'playback.cancel':
                        assert cancel_before_bound and payload['response_id'] == '1'
                    elif document['type'] == 'playback.end':
                        assert payload['samples'] == '537' and not played.is_set()
                        await send('playback.finished', {**payload, 'cancelled': False, 'error': 0})
                        await asyncio.wait_for(played.wait(), 2)
                    else:
                        pytest.fail('unexpected control kind')
                stdout, stderr = await asyncio.wait_for(child.communicate(), 8)
                assert child.returncode == 0, stderr.decode()
                assert json.loads(stdout)['reference_pcm_match']
                assert events.count('capture.stopped') == 1
                assert not transport.bridge.unexpected_failures and not transport.bootstrap.unexpected_failures
                await ws.close()
        finally:
            for task in waits:
                task.cancel()
            await asyncio.gather(*waits, return_exceptions=True)
            await stop(child)
            await stop(endpoint)
            await transport.close()
