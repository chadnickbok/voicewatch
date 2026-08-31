"""Explicit native lane: actual service main, TLS bootstrap and native process.

No hello/capture request is sent, so no provider calls or microphone PCM occur.
Process supervision evidence is separate from physical voice/UI acceptance.
"""
import asyncio
import hashlib
import json
from pathlib import Path
import socket
import tempfile

from aiohttp import ClientSession, WSServerHandshakeError
import pytest

from doodad_agent.moq_auth import bootstrap_proof
from doodad_agent.moq_supervisor import PairSupervisor, SupervisorError, load_profile
from moq_native_integration import BIN, DEVICE, KEY, probe, stop, trust, write_private


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


@pytest.mark.asyncio
@pytest.mark.parametrize('failed_role', ['agent', 'native'])
async def test_actual_pair_crash_restart_rejects_previous_grants(failed_role, monkeypatch):
    # Providers are not used; never inherit a developer's signing profile here.
    for key in ('DOODAD_PERSONAL_OWNER_ID', 'DOODAD_PERSONAL_HMAC_KEY_HEX'):
        monkeypatch.delenv(key, raising=False)
    with tempfile.TemporaryDirectory(prefix='vw-supervised-', dir='/tmp') as temporary:
        directory = Path(temporary)
        _, client_tls, roots, certificate, key_file = trust(directory)
        devices = directory/'devices.json'
        write_private(devices, json.dumps({DEVICE: KEY.hex()}).encode())
        control_port, media_port = port(), port()
        host_path = directory/'host.json'
        write_private(host_path, json.dumps(dict(certificate=str(certificate), private_key=str(key_file),
                      device_keys=str(devices), ipc_socket=str(directory/'unused.sock'),
                      public_host='127.0.0.1', media_port=media_port)).encode())
        binary = BIN/'voicewatch-moq-endpoint'
        profile_path = directory/'supervisor.json'
        write_private(profile_path, json.dumps(dict(host_config=str(host_path), endpoint_binary=str(binary),
                      endpoint_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), port=control_port,
                      database=str(directory/'bench.sqlite3'), trace=str(directory/'trace.jsonl'))).encode())
        profile, host = load_profile(profile_path)
        url = f'https://127.0.0.1:{control_port}'
        old = None
        previous = None
        async with ClientSession() as client:
            for incarnation in range(2):
                pair = PairSupervisor(profile, host)
                stopped = asyncio.Event()
                task = asyncio.create_task(pair.run(stopped))
                websocket = None
                native_probe = None
                try:
                    async with asyncio.timeout(30):
                        ready = asyncio.create_task(pair.ready.wait())
                        done, _ = await asyncio.wait((ready, task), return_when=asyncio.FIRST_COMPLETED)
                        if task in done: await task
                        await ready
                    assert pair.runtime_dir != previous
                    previous = pair.runtime_dir
                    if old is not None:
                        with pytest.raises(WSServerHandshakeError) as denied:
                            await client.ws_connect(url+'/v1/moq/control', ssl=client_tls,
                                headers={'Authorization': 'Bearer '+old['control_token']})
                        assert denied.value.status == 403
                        native_probe = await probe(dict(address=f'127.0.0.1:{media_port}', roots=str(roots),
                            setup_path=old['setup_path'], publish=old['publish'],
                            subscribe=old['subscribe'], mode='reject'), directory)
                        async with asyncio.timeout(10):
                            output, _ = await native_probe.communicate()
                        assert native_probe.returncode == 0 and json.loads(output)['rejected']
                    async with client.post(url+'/v1/moq/challenge', ssl=client_tls,
                                           json={'device_id': DEVICE}) as response:
                        nonce = (await response.json())['challenge']
                    async with client.post(url+'/v1/moq/bootstrap', ssl=client_tls, json=dict(
                            device_id=DEVICE, challenge=nonce, proof=bootstrap_proof(KEY, DEVICE, nonce))) as response:
                        assert response.status == 200
                        grant = await response.json()
                    websocket = await client.ws_connect(url+'/v1/moq/control', ssl=client_tls,
                                  headers={'Authorization': 'Bearer '+grant['control_token']})
                    if old is not None:
                        assert old['session_id'] != grant['session_id']
                    old = grant
                    if incarnation == 0:
                        pair.children[0 if failed_role == 'agent' else 1].kill()
                        with pytest.raises(SupervisorError): await asyncio.wait_for(task, 15)
                    else:
                        stopped.set()
                        await asyncio.wait_for(task, 15)
                    async with asyncio.timeout(3):
                        while not websocket.closed: await websocket.receive()
                finally:
                    stopped.set()
                    await asyncio.gather(task, return_exceptions=True)
                    if websocket is not None: await websocket.close()
                    await stop(native_probe)
                assert all(child.returncode is not None for child in pair.children)
                assert not pair.runtime_dir.exists()
