"""Service configuration and existing conversation boundary, no provider calls."""
import json
import os
import asyncio
import tempfile
from pathlib import Path

import pytest
from aiohttp import ClientSession, WSMsgType, web

from doodad_agent.main import parse_arguments, complete_capture_to_conversation
from doodad_agent.moq_config import MoqConfigError, MoqHostConfig
from doodad_agent.moq_auth import bootstrap_proof
from doodad_agent.metrics import LatencyTrace
from doodad_agent.transport_moq import MoqTransportServer
from test_moq_host import tls_contexts


def config_file(directory: Path) -> Path:
    tls_contexts(directory)
    devices = directory/'devices.json'
    devices.write_text(json.dumps({'watch-service-test': bytes(range(32)).hex()}))
    devices.chmod(0o600)
    config = directory/'host.json'
    config.write_text(json.dumps(dict(certificate=str(directory/'cert.pem'),
                                     private_key=str(directory/'key.pem'), device_keys=str(devices),
                                     ipc_socket=str(directory/'agent.sock'), public_host='127.0.0.1', media_port=4443)))
    config.chmod(0o600)
    return config


def test_explicit_mode_requires_config_and_legacy_remains_explicit_default():
    assert parse_arguments(['serve']).transport == 'webrtc'
    parsed = parse_arguments(['serve', '--transport', 'moq', '--moq-config', '/private/test.json'])
    assert parsed.transport == 'moq' and parsed.moq_config == Path('/private/test.json')
    with pytest.raises(SystemExit):
        parse_arguments(['serve', '--transport', 'moq'])
    with pytest.raises(SystemExit):
        parse_arguments(['serve', '--moq-config', '/private/test.json'])


def test_private_configuration_loads_tls_and_registry(tmp_path):
    config = MoqHostConfig.load(config_file(tmp_path))
    assert config.public_host == '127.0.0.1' and config.media_port == 4443
    assert config.registry.challenge('watch-service-test')
    assert config.ipc_path == tmp_path/'agent.sock'


@pytest.mark.parametrize('fault', ['public_config', 'public_key', 'symlink', 'fifo', 'duplicate',
                                 'port_bool', 'relative', 'unknown', 'pem', 'certificate_size', 'devices_fifo'])
def test_config_fails_closed_without_printing_private_data(tmp_path, fault):
    path = config_file(tmp_path)
    body = json.loads(path.read_bytes())
    if fault == 'public_config': path.chmod(0o644)
    elif fault == 'public_key': (tmp_path/'key.pem').chmod(0o644)
    elif fault == 'symlink':
        link = tmp_path/'link'; link.symlink_to(path); path = link
    elif fault == 'fifo':
        path = tmp_path/'fifo'; os.mkfifo(path, 0o600)
    elif fault == 'devices_fifo':
        devices = tmp_path/'devices.json'; devices.unlink(); os.mkfifo(devices, 0o600)
    elif fault == 'duplicate': path.write_text('{"private_value":"HIDDEN","private_value":"HIDDEN"}')
    elif fault == 'pem': (tmp_path/'key.pem').write_text('HIDDEN_INVALID_PEM')
    elif fault == 'certificate_size': (tmp_path/'cert.pem').write_bytes(b'x' * 65537)
    else:
        if fault == 'port_bool': body['media_port'] = True
        elif fault == 'relative': body['ipc_socket'] = 'relative.sock'
        else: body['private_value'] = 'HIDDEN'
        path.write_text(json.dumps(body))
    with pytest.raises(MoqConfigError) as error:
        MoqHostConfig.load(path)
    assert str(error.value) == 'invalid private MoQ host configuration'
    assert error.value.__suppress_context__


@pytest.mark.asyncio
@pytest.mark.parametrize('moq', [False, True])
async def test_ptt_commit_waits_for_validated_moq_audio_and_preserves_legacy_padding(moq):
    class Session:
        explicit_capture_completion = moq
    class Conversation:
        def __init__(self): self.pcm = bytearray(); self.committed = None
        async def feed_audio(self, pcm): self.pcm.extend(pcm)
        async def capture_completed(self): self.committed = bytes(self.pcm)
    conversation = Conversation()
    session = Session()
    await complete_capture_to_conversation(conversation, session, 'listen.finished')
    assert len(conversation.pcm) == (0 if moq else 8000)
    assert conversation.committed is None
    await conversation.feed_audio(b'\x05\x00' * 217)
    await complete_capture_to_conversation(conversation, session, 'capture.stopped')
    if moq:
        assert conversation.pcm == b'\x05\x00' * 217
        assert conversation.committed == bytes(conversation.pcm)
    else:
        assert conversation.pcm == b'\0'*8000 + b'\x05\x00' * 217
        assert conversation.committed is None


@pytest.mark.asyncio
@pytest.mark.parametrize('fault', ['identity', 'replay', 'duplicate', 'replacement'])
async def test_real_wss_contract_retires_bad_sessions_and_keeps_replacement(fault):
    with tempfile.TemporaryDirectory(prefix='vw-wss-', dir='/tmp') as temporary:
        directory = Path(temporary)
        path = config_file(directory)
        config = MoqHostConfig.load(path)
        import ssl
        client_context = ssl.create_default_context(cafile=str(directory/'cert.pem'))
        events = []
        async def audio(*_): pytest.fail('no media is authorized in this test')
        async def event(device, kind, payload): events.append(kind)
        class Artifacts:
            def add_routes(self, app):
                async def get(_): return web.Response(text='test artifact route')
                app.router.add_get('/apps/test-route', get)
        transport = MoqTransportServer(LatencyTrace(), audio, event, 0, registry=config.registry,
                                      context=config.context, ipc_path=config.ipc_path,
                                      media_host='127.0.0.1', media_port=4443, host='127.0.0.1',
                                      artifact_server=Artifacts())
        await transport.start()
        base = f'https://127.0.0.1:{transport.bootstrap._runner.addresses[0][1]}'
        try:
            async with ClientSession() as client:
                async def connect():
                    device = 'watch-service-test'
                    async with client.post(base+'/v1/moq/challenge', json={'device_id': device}, ssl=client_context) as response:
                        nonce = (await response.json())['challenge']
                    async with client.post(base+'/v1/moq/bootstrap', ssl=client_context, json=dict(
                        device_id=device, challenge=nonce, proof=bootstrap_proof(bytes(range(32)), device, nonce))) as response:
                        grant = await response.json()
                    ws = await client.ws_connect(base+'/v1/moq/control', ssl=client_context,
                                                 headers={'Authorization': 'Bearer '+grant['control_token']})
                    hello = dict(v=1, seq=1, type='hello', device_id=device, session_id=grant['session_id'],
                                 payload=dict(device_id=device, board='t-watch-ultra', transport='moq', capabilities={}))
                    await ws.send_json(hello)
                    assert (await ws.receive_json(timeout=2))['type'] == 'welcome'
                    return ws, hello
                async with client.get(base+'/apps/test-route', ssl=client_context) as response:
                    assert response.status == 200 and await response.text() == 'test artifact route'
                old, hello = await connect()
                if fault == 'replacement':
                    new, current = await connect()
                    message = await old.receive(timeout=2)
                    assert message.type in {WSMsgType.CLOSE, WSMsgType.CLOSED}
                    async with asyncio.timeout(2):
                        while hello['session_id'] in transport._pending:
                            await asyncio.sleep(0.005)
                    assert transport.sessions[hello['device_id']].session_id == current['session_id']
                    assert 'disconnected' not in events
                    await new.close()
                else:
                    document = {**hello, 'seq': 2, 'type': 'watch.state', 'payload': {}}
                    if fault == 'identity': document['device_id'] = 'watch-another-device'
                    if fault == 'replay': document['seq'] = 1
                    if fault == 'duplicate':
                        await old.send_str(json.dumps(document)[:-1] + ',"seq":2}')
                    else:
                        await old.send_json(document)
                    assert (await old.receive(timeout=2)).type in {WSMsgType.CLOSE, WSMsgType.CLOSED}
                    async with asyncio.timeout(2):
                        while transport.sessions:
                            await asyncio.sleep(0.005)
                    assert 'watch.state' not in events
                await old.close()
                assert not transport.bootstrap.unexpected_failures
        finally:
            await transport.close()
