"""Boundary tests use generated test keys/PCM, never enrollment or ambient audio."""
from __future__ import annotations

import asyncio
import json
import os
import ssl
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
from pathlib import Path

import numpy as np
import pytest
import soxr
from aiohttp import ClientSession, WSMsgType, ClientConnectorCertificateError
from aiohttp.test_utils import TestServer
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

from doodad_agent.audio import AudioInterrupted, PcmSpool, _PacketPacer
from doodad_agent.metrics import LatencyTrace
from doodad_agent.moq_auth import AuthorizationError, GrantRegistry, bootstrap_proof, load_device_keys
from doodad_agent.moq_bootstrap import MoqBootstrap
from doodad_agent.moq_ipc import IpcError, IpcWriter, Packet, encode, read_packet

A, B = 'watch-ultra-a', 'watch-ultra-b'
KEYS = {A: bytes(range(32)), B: bytes(range(1, 33))}


class Clock:
    mono = 0.0
    wall = 1_800_000_000.0

    async def sleep(self, seconds):
        self.mono += seconds

    def advance(self, seconds):
        self.mono += seconds
        self.wall += seconds


@pytest.fixture
def auth():
    clock = Clock()
    return GrantRegistry(KEYS, monotonic=lambda: clock.mono, wall_clock=lambda: clock.wall), clock


@pytest.fixture
def socket_dir():
    # macOS sun_path is 104 bytes; pytest's descriptive tmp_path exceeds it.
    with tempfile.TemporaryDirectory(prefix='vw-ipc-', dir='/tmp') as directory:
        yield Path(directory)


def issue(registry, device=A):
    nonce = registry.challenge(device)
    return registry.issue(device, nonce, bootstrap_proof(KEYS[device], device, nonce))


def test_grants_require_live_control_and_are_direction_scoped_and_one_use(auth):
    registry, _ = auth
    a, b = issue(registry), issue(registry, B)
    control, media = object(), object()
    with pytest.raises(AuthorizationError):
        registry.attach_media(a.media_token, media)
    sid = registry.activate_control(a.control_token, control)
    assert registry.identity(sid, control) == A
    assert not registry.valid(sid, None)
    with pytest.raises(AuthorizationError):
        registry.activate_control(a.control_token, object())
    with pytest.raises(AuthorizationError):
        registry.attach_media(a.control_token, media)
    result = registry.attach_media(a.media_token, media)
    assert result['session_id'] == a.session_id
    assert result['publish'] == f'voicewatch/{A}/{sid}/watch'
    assert result['subscribe'] == f'voicewatch/{A}/{sid}/agent'
    assert result['publish'] != b.publish
    assert result['lease_ms'] == 300_000
    with pytest.raises(AuthorizationError):
        registry.attach_media(a.media_token, object())
    with pytest.raises(AuthorizationError):
        registry.identity(sid, object())
    assert registry.valid(sid, media)
    registry.revoke(sid, object())
    assert registry.valid(sid, media)
    registry.revoke(sid, control)
    assert not registry.valid(sid, media)


def test_replacement_revokes_only_matching_device_and_old_cleanup_is_safe(auth):
    registry, _ = auth
    original, other = issue(registry), issue(registry, B)
    owner, peer, other_owner = object(), object(), object()
    registry.activate_control(original.control_token, owner)
    registry.attach_media(original.media_token, peer)
    registry.activate_control(other.control_token, other_owner)
    fresh = issue(registry)
    fresh_owner = object()
    assert registry.valid(original.session_id, peer)  # Proof alone does not take over.
    registry.activate_control(fresh.control_token, fresh_owner)
    assert not registry.valid(original.session_id, peer)
    assert registry.valid(other.session_id, other_owner)
    registry.revoke(original.session_id, owner)
    assert registry.valid(fresh.session_id, fresh_owner)


def test_nonce_replay_wrong_key_cross_device_and_wrong_proof_are_rejected(auth):
    registry, _ = auth
    nonce = registry.challenge(A)
    with pytest.raises(AuthorizationError):
        registry.issue(B, nonce, bootstrap_proof(KEYS[A], A, nonce))
    with pytest.raises(AuthorizationError):
        registry.issue(A, nonce, bootstrap_proof(KEYS[B], A, nonce))
    with pytest.raises(AuthorizationError):
        registry.issue(A, nonce, bootstrap_proof(KEYS[A], A, nonce))
    nonce = registry.challenge(A)
    proof = bootstrap_proof(KEYS[A], A, nonce)
    registry.issue(A, nonce, proof)
    with pytest.raises(AuthorizationError):
        registry.issue(A, nonce, proof)
    with pytest.raises(AuthorizationError):
        registry.challenge('unknown-device')


@pytest.mark.parametrize('advance', [30, 301])
def test_unattached_grants_expire(auth, advance):
    registry, clock = auth
    grant = issue(registry)
    clock.advance(advance)
    with pytest.raises(AuthorizationError):
        registry.activate_control(grant.control_token, object())


@pytest.mark.parametrize('fault', ['lease', 'wall-forward', 'wall-rollback', 'mono-rollback'])
def test_live_lease_and_clock_faults_retire_both_channels(auth, fault):
    registry, clock = auth
    grant, control, peer = issue(registry), object(), object()
    registry.activate_control(grant.control_token, control)
    registry.attach_media(grant.media_token, peer)
    if fault == 'lease': clock.advance(300)
    elif fault == 'wall-forward': clock.wall += 301
    elif fault == 'wall-rollback': clock.wall -= 2
    else: clock.mono -= 1
    assert not registry.valid(grant.session_id, peer)
    assert not registry.valid(grant.session_id, control)


def test_capacity_is_bounded_and_credentials_are_not_in_reprs_or_errors():
    registry = GrantRegistry(KEYS, capacity=1)
    grant = issue(registry)
    with pytest.raises(AuthorizationError) as failure:
        issue(registry, B)
    for secret in (grant.control_token, grant.media_token):
        assert secret not in repr(grant)
        assert secret not in str(failure.value)
        assert secret not in repr(Packet({'v': 1, 'type': 'attach', 'token': secret}))


def test_enrollment_permissions_and_unique_keys(tmp_path):
    path = tmp_path/'devices.json'
    path.write_text(json.dumps({key: value.hex() for key, value in KEYS.items()}))
    path.chmod(0o600)
    assert load_device_keys(path) == KEYS
    link = tmp_path/'link'; link.symlink_to(path)
    with pytest.raises(OSError): load_device_keys(link)
    path.chmod(0o644)
    with pytest.raises(ValueError): load_device_keys(path)
    path.chmod(0o600)
    path.write_text(json.dumps({A: KEYS[A].hex(), B: KEYS[A].hex()}))
    with pytest.raises(ValueError): load_device_keys(path)


def tls_contexts(tmp_path):
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'VoiceWatch test only')])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now-timedelta(minutes=1)).not_valid_after(now+timedelta(hours=1))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ip_address('127.0.0.1'))]), critical=False)
            .sign(key, hashes.SHA256()))
    cert_path, key_path = tmp_path/'cert.pem', tmp_path/'key.pem'
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                          serialization.NoEncryption()))
    key_path.chmod(0o600)
    server = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server.minimum_version = ssl.TLSVersion.TLSv1_2
    server.load_cert_chain(cert_path, key_path)
    client = ssl.create_default_context(cafile=str(cert_path))
    return server, client


@pytest.mark.asyncio
async def test_real_tls_bootstrap_wss_binding_revocation_and_token_replay(tmp_path, auth):
    registry, _ = auth
    seen = asyncio.Queue()

    async def handler(ws, sid, owner):
        await seen.put((sid, owner))
        async for message in ws:
            if message.type == WSMsgType.TEXT:
                await ws.send_str(registry.identity(sid, owner))

    bootstrap = MoqBootstrap(registry, handler, media_host='127.0.0.1', media_port=4443)
    server_context, client_context = tls_contexts(tmp_path)
    server = TestServer(bootstrap.application(), scheme='https')
    await server.start_server(ssl=server_context)
    try:
        async with ClientSession() as client:
            with pytest.raises(ClientConnectorCertificateError):
                await client.post(server.make_url('/v1/moq/challenge'), json={'device_id': A})
            async with client.post(server.make_url('/v1/moq/challenge'), json={'device_id': A}, ssl=client_context) as response:
                assert response.status == 200
                nonce = (await response.json())['challenge']
            body = {'device_id': A, 'challenge': nonce, 'proof': bootstrap_proof(KEYS[A], A, nonce)}
            async with client.post(server.make_url('/v1/moq/bootstrap'), json=body, ssl=client_context) as response:
                assert response.status == 200
                assert response.headers['Cache-Control'] == 'no-store'
                grant = await response.json()
            async with client.post(server.make_url('/v1/moq/bootstrap'), json=body, ssl=client_context) as response:
                assert response.status == 403
            token = grant['control_token']
            async with client.get(server.make_url('/v1/moq/control?token='+token), ssl=client_context) as response:
                assert response.status == 403
                assert token not in await response.text()
            ws = await client.ws_connect(server.make_url('/v1/moq/control'),
                                         headers={'Authorization': 'Bearer '+token}, ssl=client_context)
            sid, owner = await asyncio.wait_for(seen.get(), 1)
            assert sid == grant['session_id']
            await ws.send_str('identity')
            assert (await ws.receive()).data == A
            media = object()
            media_token = grant['setup_path'].split('token=')[1]
            assert registry.attach_media(media_token, media)['session_id'] == sid
            async with client.get(server.make_url('/v1/moq/control'), headers={'Authorization': 'Bearer '+token}, ssl=client_context) as response:
                assert response.status == 403
            await ws.close()
            for _ in range(20):
                if not registry.valid(sid, media): break
                await asyncio.sleep(0.01)
            assert not registry.valid(sid, media)
    finally:
        await server.close()


@pytest.mark.asyncio
async def test_plain_http_and_forwarded_tls_do_not_authorize(auth):
    registry, _ = auth
    async def unused(*args): raise AssertionError('control must not open')
    app = MoqBootstrap(registry, unused, media_host='localhost', media_port=4443).application()
    async with TestServer(app) as server, ClientSession() as client:
        for path in ('challenge', 'bootstrap', 'control'):
            async with client.request('GET' if path == 'control' else 'POST', server.make_url('/v1/moq/'+path),
                                      json={'device_id': A}, headers={'X-Forwarded-Proto': 'https'}) as response:
                assert response.status == 403


@pytest.mark.asyncio
async def test_ipc_reads_fragmented_binary_pcm_without_loss():
    pcm = np.arange(320, dtype='<i2').tobytes()
    packet = Packet({'v': 1, 'type': 'capture.pcm', 'pcm_bytes': len(pcm)}, pcm)
    raw = encode(packet)
    reader = asyncio.StreamReader(limit=8192)
    result = asyncio.create_task(read_packet(reader))
    for start in range(0, len(raw), 7):
        reader.feed_data(raw[start:start+7])
        await asyncio.sleep(0)
    assert await result == packet


@pytest.mark.asyncio
@pytest.mark.parametrize('body', [
    struct.pack('!I', 4097), struct.pack('!I', 0),
    b'{"v":1,"type":"capture.pcm","pcm_bytes":642}',
    b'{"v":1,"type":"capture.pcm","pcm_bytes":3}',
    b'{"v":1,"v":1,"type":"ping"}',
    b'{"v":true,"type":"ping"}',
    b'{"v":1,"type":"ping","pcm_bytes":2}',
    b'{"v":1,"type":"ping","bad":NaN}',
    b'[]', b'\xff',
])
async def test_ipc_rejects_oversize_ambiguous_or_invalid_headers_before_pcm_read(body):
    reader = asyncio.StreamReader()
    raw = body if len(body) == 4 else struct.pack('!I', len(body))+body
    reader.feed_data(raw)
    with pytest.raises(IpcError):
        await asyncio.wait_for(read_packet(reader), 0.1)


@pytest.mark.asyncio
async def test_ipc_mid_frame_eof_and_stall_are_terminal():
    reader = asyncio.StreamReader()
    reader.feed_data(struct.pack('!I', 8)+b'{}')
    reader.feed_eof()
    with pytest.raises(IpcError): await read_packet(reader)
    with pytest.raises(IpcError): await read_packet(asyncio.StreamReader(), timeout=0.01)


@pytest.mark.asyncio
async def test_neutral_spool_preserves_resampler_tail_and_logical_sample_count():
    clock = Clock()
    spool = PcmSpool(LatencyTrace())
    spool._pacer = _PacketPacer(clock=lambda: clock.mono, sleep=clock.sleep)
    source = (np.sin(np.arange(1247)*0.05)*14000).astype('<i2')
    spool.begin_utterance()
    generation = spool.generation
    accepted = 0
    for start in range(0, len(source), 97):
        accepted += spool.enqueue_pcm(source[start:start+97].tobytes(), 24000)
    accepted += spool.end_utterance()
    assert spool.end_utterance() == 0
    chunks = []
    while (packet := await spool.read(generation)) is not None:
        chunks.append(packet.data)
    expected = soxr.resample(source, 24000, 16000, quality='HQ').astype('<i2')
    actual = np.frombuffer(b''.join(chunks), dtype='<i2')
    assert accepted == len(actual) == len(expected)
    assert len(chunks[-1]) < 640  # No silent extension to a whole Opus frame.
    np.testing.assert_allclose(actual, expected, atol=2)
    assert spool.pending_ms == 0


@pytest.mark.asyncio
async def test_cancel_during_pacer_cannot_leak_old_audio_into_replacement():
    entered, released = asyncio.Event(), asyncio.Event()
    async def blocked(seconds):
        entered.set()
        await released.wait()
    spool = PcmSpool(LatencyTrace())
    spool._pacer = _PacketPacer(clock=lambda: 0, sleep=blocked)
    spool.begin_utterance()
    old = spool.generation
    spool.enqueue_pcm(b'\x01\x00'*640, 16000)
    await spool.read(old)
    stale = asyncio.create_task(spool.read(old))
    await entered.wait()
    spool.clear()
    spool.begin_utterance()
    spool.enqueue_pcm(b'\x02\x00'*320, 16000)
    spool.end_utterance()
    released.set()
    with pytest.raises(AudioInterrupted): await stale
    fresh = await spool.read(spool.generation)
    assert fresh.data == b'\x02\x00'*320
    assert await spool.read(spool.generation) is None


def test_neutral_imports_do_not_require_aiortc_or_av():
    code = '''
import sys
sys.modules['aiortc'] = None
sys.modules['av'] = None
from doodad_agent.audio import PcmSpool
from doodad_agent.session import ControlSession, DownlinkUtteranceBinding
from doodad_agent.moq_auth import GrantRegistry
from doodad_agent.moq_bootstrap import MoqBootstrap
from doodad_agent.moq_ipc import read_packet
from doodad_agent.main import parse_arguments
'''
    # Only test the modules we deliberately make neutral; main may have other
    # product dependencies but must not load the legacy media transport eagerly.
    code = code.replace('from doodad_agent.main import parse_arguments', 'import doodad_agent.main')
    subprocess.run([sys.executable, '-c', code], check=True, capture_output=True)


@pytest.mark.asyncio
async def test_real_unix_ipc_auth_identity_pcm_and_disconnect_revoke_control(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    grant, control = issue(registry), object()
    registry.activate_control(grant.control_token, control)
    attached, incoming, closed = asyncio.Queue(), asyncio.Queue(), asyncio.Queue()
    async def on_attach(peer): await attached.put(peer)
    async def on_packet(peer, packet): await incoming.put((peer, packet))
    async def on_close(peer): await closed.put(peer.session_id)
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, on_attach, on_packet, on_close)
    await bridge.start()
    try:
        reader, writer = await asyncio.open_unix_connection(bridge.path)
        client = IpcWriter(writer)
        await client.send({'type': 'attach', 'token': grant.media_token})
        authorized = await read_packet(reader)
        assert authorized.header['type'] == 'authorized'
        assert authorized.header['session_id'] == grant.session_id
        assert 'token' not in authorized.header
        peer = await asyncio.wait_for(attached.get(), 1)
        pcm = np.arange(320, dtype='<i2').tobytes()
        await client.send({'type': 'capture.pcm', 'session_id': grant.session_id, 'seq': 1}, pcm)
        source, received = await asyncio.wait_for(incoming.get(), 1)
        assert source is peer and received.pcm == pcm
        await peer.send('capture.stop', {'capture_id': '71'})
        assert (await read_packet(reader)).header['capture_id'] == '71'
        await client.close()
        assert await asyncio.wait_for(closed.get(), 1) == grant.session_id
        assert not registry.valid(grant.session_id, control)
        assert bridge.unexpected_failures == 0
    finally:
        await bridge.close()
    assert not bridge.path.exists()


@pytest.mark.asyncio
@pytest.mark.parametrize('fault', ['wrong-session', 'replayed-sequence', 'expiry', 'control-loss'])
async def test_ipc_retires_on_cross_session_replay_or_control_lease_loss(socket_dir, auth, fault):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, clock = auth
    grant, control = issue(registry), object()
    registry.activate_control(grant.control_token, control)
    packets, closed = [], asyncio.Event()
    async def on_attach(peer): pass
    async def on_packet(peer, packet): packets.append(packet)
    async def on_close(peer): closed.set()
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, on_attach, on_packet, on_close)
    await bridge.start()
    try:
        reader, writer = await asyncio.open_unix_connection(bridge.path)
        client = IpcWriter(writer)
        await client.send({'type': 'attach', 'token': grant.media_token})
        await read_packet(reader)
        if fault == 'expiry': clock.advance(301)
        elif fault == 'control-loss': registry.revoke(grant.session_id, control)
        else:
            await client.send({'type': 'ping', 'session_id': 'different' if fault == 'wrong-session' else grant.session_id,
                               'seq': 0 if fault == 'replayed-sequence' else 1})
        await asyncio.wait_for(closed.wait(), 1)
        assert not packets
        assert not registry.valid(grant.session_id, control)
        assert await reader.read() == b''
        await client.close()
    finally:
        await bridge.close()


@pytest.mark.asyncio
async def test_ipc_requires_private_directory_and_never_removes_an_existing_path(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    async def unused(*args): pass
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, unused, unused, unused)
    socket_dir.chmod(0o755)
    with pytest.raises(ValueError): await bridge.start()
    socket_dir.chmod(0o700)
    bridge.path.write_text('unrelated file')
    with pytest.raises(FileExistsError): await bridge.start()
    await bridge.close()
    assert bridge.path.read_text() == 'unrelated file'


@pytest.mark.asyncio
async def test_unauthenticated_ipc_never_reaches_application(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    callbacks = []
    async def unexpected(*args): callbacks.append(args)
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, unexpected, unexpected, unexpected)
    await bridge.start()
    try:
        reader, writer = await asyncio.open_unix_connection(bridge.path)
        client = IpcWriter(writer)
        grant = issue(registry)  # Valid proof, but no authenticated WSS owner.
        await client.send({'type': 'attach', 'token': grant.media_token})
        assert await asyncio.wait_for(reader.read(), 1) == b''
        assert not callbacks
        await client.close()
    finally:
        await bridge.close()


@pytest.mark.asyncio
async def test_spool_overflow_after_resampling_retires_generation():
    spool = PcmSpool(LatencyTrace(), max_spool_seconds=1)
    spool.begin_utterance()
    generation = spool.generation
    with pytest.raises(BufferError):
        spool.enqueue_pcm(bytes(48000*2), 24000)
    with pytest.raises(AudioInterrupted):
        await spool.read(generation)
    assert spool.pending_ms == 0
    assert spool.end_utterance() == 0
    with pytest.raises(RuntimeError): spool.enqueue_pcm(bytes(20), 24000)


def test_enrollment_rejects_duplicate_device_entries(tmp_path):
    path = tmp_path/'devices.json'
    entry = json.dumps(A)+':'+json.dumps(KEYS[A].hex())
    path.write_text('{'+entry+','+entry+'}')
    path.chmod(0o600)
    with pytest.raises(ValueError, match='duplicate'): load_device_keys(path)


def test_bootstrap_hmac_matches_independent_openssl_vector():
    # openssl dgst -sha256 -mac HMAC with hexkey=000102...1f and the
    # documented NUL-delimited byte sequence, not a second call to this issuer.
    assert bootstrap_proof(KEYS[A], A, 'A'*43) == 'a2e1bdda78efc6a3aede42c4b2befdd17c4f8bfee35a6de9fbe855ccceb7ebfe'


@pytest.mark.asyncio
async def test_shared_action_contract_keeps_idempotency_and_disconnect_errors():
    from doodad_agent.session import ControlSession
    sent = asyncio.Queue()
    class Socket:
        async def send_str(self, document): await sent.put(json.loads(document))
    async def unused(*args): pass
    session = ControlSession(Socket(), LatencyTrace(), unused, unused, unused)
    session.device_id = A
    action = asyncio.create_task(session.invoke_action('get_next_set', {}, 'x'*100))
    message = await sent.get()
    request = message['payload']['request_id']
    assert len(request.encode()) < 65
    assert message['payload']['idempotency_key'] == request
    assert message['device_id'] == A
    session._pending_actions[request].set_result({'ok': True, 'result': {'value': 7}, 'duplicate': True})
    assert await action == {'value': 7, 'duplicate': True}
    action = asyncio.create_task(session.invoke_action('get_next_set', {}, 'pending'))
    await sent.get()
    session._fail_pending_actions()
    with pytest.raises(ConnectionError): await action
    assert not session._pending_actions


@pytest.mark.asyncio
async def test_bridge_start_does_not_replace_a_live_socket(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    async def unused(*args): pass
    first = MoqBridgeServer(socket_dir/'media.sock', registry, unused, unused, unused)
    second = MoqBridgeServer(first.path, registry, unused, unused, unused)
    await first.start()
    inode = first.path.stat().st_ino
    try:
        with pytest.raises(FileExistsError): await second.start()
        await second.close()
        assert first.path.stat().st_ino == inode
    finally:
        await first.close()


@pytest.mark.asyncio
async def test_ipc_callback_failure_is_closed_without_logging_private_exception(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    grant, control = issue(registry), object()
    registry.activate_control(grant.control_token, control)
    closed = asyncio.Event()
    async def fail(peer): raise RuntimeError(grant.media_token)
    async def packet(*args): raise AssertionError('must not process media')
    async def close(peer): closed.set()
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, fail, packet, close)
    await bridge.start()
    try:
        reader, writer = await asyncio.open_unix_connection(bridge.path)
        client = IpcWriter(writer)
        await client.send({'type': 'attach', 'token': grant.media_token})
        await read_packet(reader)
        await asyncio.wait_for(closed.wait(), 1)
        assert await reader.read() == b''
        assert not registry.valid(grant.session_id, control)
        assert bridge.unexpected_failures == 1
        await client.close()
    finally:
        await bridge.close()


@pytest.mark.asyncio
async def test_bootstrap_serving_boundary_requires_tls_and_disables_access_logging(tmp_path, auth):
    registry, _ = auth
    async def unused(*args): pass
    bootstrap = MoqBootstrap(registry, unused, media_host='127.0.0.1', media_port=4443)
    insecure = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    with pytest.warns(DeprecationWarning):
        insecure.minimum_version = ssl.TLSVersion.TLSv1
    with pytest.raises(ValueError): await bootstrap.start('127.0.0.1', 0, insecure)
    server, _ = tls_contexts(tmp_path)
    await bootstrap.start('127.0.0.1', 0, server)
    assert bootstrap._runner is not None
    assert bootstrap._runner._kwargs['access_log'] is None
    await bootstrap.close()
    assert bootstrap._runner is None


@pytest.mark.asyncio
async def test_ipc_writer_rejects_concurrent_unbounded_waiters_and_closes_on_stall(monkeypatch):
    import doodad_agent.moq_ipc as ipc
    class Transport:
        def set_write_buffer_limits(self, **kwargs): pass
        def abort(self): pass
    class Writer:
        def __init__(self):
            self.transport, self.gate = Transport(), asyncio.Event()
            self.closed, self.writes = False, []
        def is_closing(self): return self.closed
        def write(self, data): self.writes.append(data)
        async def drain(self): await self.gate.wait()
        def close(self): self.closed = True
    raw = Writer()
    writer = IpcWriter(raw)
    pending = asyncio.create_task(writer.send({'type': 'ping'}))
    await asyncio.sleep(0)
    with pytest.raises(IpcError): await writer.send({'type': 'ping'})
    assert len(raw.writes) == 1
    raw.gate.set()
    await pending
    raw.gate.clear()
    monkeypatch.setattr(ipc, 'WRITE_TIMEOUT', 0.01)
    with pytest.raises(IpcError): await writer.send({'type': 'ping'})
    assert raw.closed


@pytest.mark.asyncio
async def test_ipc_connection_limit_rejects_excess_peers(socket_dir, auth):
    from doodad_agent.moq_bridge import MoqBridgeServer
    registry, _ = auth
    grant, control = issue(registry), object()
    registry.activate_control(grant.control_token, control)
    attached = asyncio.Event()
    async def on_attach(peer): attached.set()
    async def unused(*args): pass
    bridge = MoqBridgeServer(socket_dir/'media.sock', registry, on_attach, unused, unused, max_peers=1)
    await bridge.start()
    try:
        reader, writer = await asyncio.open_unix_connection(bridge.path)
        client = IpcWriter(writer)
        await client.send({'type': 'attach', 'token': grant.media_token})
        await read_packet(reader)
        await attached.wait()
        second_reader, second_writer = await asyncio.open_unix_connection(bridge.path)
        assert await asyncio.wait_for(second_reader.read(), 1) == b''
        assert len(bridge._tasks) == 1
        second_writer.close()
        await second_writer.wait_closed()
        await client.close()
    finally:
        await bridge.close()


@pytest.mark.asyncio
async def test_real_wss_liveness_closes_expired_control_and_media(tmp_path, auth):
    registry, clock = auth
    identified = asyncio.Queue()
    async def handler(ws, sid, owner):
        await identified.put((sid, owner))
        async for _ in ws: pass
    bootstrap = MoqBootstrap(registry, handler, media_host='127.0.0.1', media_port=4443)
    server_context, client_context = tls_contexts(tmp_path)
    server = TestServer(bootstrap.application(), scheme='https')
    await server.start_server(ssl=server_context)
    try:
        grant = issue(registry)
        async with ClientSession() as client:
            ws = await client.ws_connect(server.make_url('/v1/moq/control'),
                                         headers={'Authorization': 'Bearer '+grant.control_token}, ssl=client_context)
            sid, owner = await asyncio.wait_for(identified.get(), 1)
            media = object()
            registry.attach_media(grant.media_token, media)
            clock.advance(301)
            event = await asyncio.wait_for(ws.receive(), 1)
            assert event.type == WSMsgType.CLOSE
            assert event.data == 4003
            assert not registry.valid(sid, media)
            await ws.close()
    finally:
        await server.close()
