"""Real child processes exercise ownership; fixtures make no provider calls."""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import signal
import sys

import pytest

from doodad_agent.moq_supervisor import PairSupervisor, SupervisorError, load_profile, serve
from test_moq_service import config_file


CHILD = '''
import asyncio,json,os,signal,sys
from pathlib import Path
from doodad_agent.moq_supervision import ParentLink
async def main():
    root=Path(__file__).parent
    native=sys.argv[1]=='--config'
    role='native' if native else 'agent'
    mode=json.loads((root/'mode.json').read_text())
    link=ParentLink.inherited()
    stopped=asyncio.Event()
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, stopped.set)
    (root/(role+'.json')).write_text(json.dumps(dict(pid=os.getpid(),
        provider_key='OPENAI_API_KEY' in os.environ,
        parent_env='DOODAD_MOQ_SUPERVISOR_FD' in os.environ,
        inheritable=link.sock.get_inheritable())))
    if mode.get('hang')==role:
        await stopped.wait()
    else:
        await link.ready(stopped)
        await stopped.wait()
    await link.close()
    if mode.get('ignore_stop')==role:
        await asyncio.Event().wait()
asyncio.run(main())
'''


def fixture_profile(tmp_path, mode=None):
    host_path = config_file(tmp_path)
    binary = tmp_path/'child'
    binary.write_text(f'#!{sys.executable}\n'+CHILD)
    binary.chmod(0o700)
    (tmp_path/'mode.json').write_text(json.dumps(mode or {}))
    profile = dict(host_config=str(host_path), endpoint_binary=str(binary),
                   endpoint_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                   port=8765, database=str(tmp_path/'bench.sqlite3'), trace=str(tmp_path/'trace.jsonl'))
    path = tmp_path/'supervisor.json'
    path.write_text(json.dumps(profile)); path.chmod(0o600)
    return path


def pair_for(path):
    profile, host = load_profile(path)
    return PairSupervisor(profile, host, agent_command=[sys.executable, profile['endpoint_binary']],
                          startup_timeout=2, shutdown_timeout=0.3)


@pytest.mark.parametrize('fault', ['hash', 'public_profile', 'binary_symlink', 'binary_fifo',
                                  'binary_writable', 'port_bool', 'relative', 'unknown', 'time_collision'])
def test_private_profile_rejects_bad_configuration_without_private_text(tmp_path, fault):
    path = fixture_profile(tmp_path)
    raw = json.loads(path.read_text())
    if fault == 'hash': raw['endpoint_sha256'] = '0'*64
    elif fault == 'public_profile': path.chmod(0o644)
    elif fault == 'binary_symlink':
        link = tmp_path/'link'; link.symlink_to(raw['endpoint_binary']); raw['endpoint_binary'] = str(link)
    elif fault == 'binary_fifo':
        fifo = tmp_path/'fifo'; os.mkfifo(fifo, 0o600); raw['endpoint_binary'] = str(fifo)
    elif fault == 'binary_writable': Path(raw['endpoint_binary']).chmod(0o777)
    elif fault == 'port_bool': raw['port'] = True
    elif fault == 'relative': raw['trace'] = 'private_value'
    elif fault == 'time_collision':
        host = Path(raw['host_config']); body = json.loads(host.read_text()); body['time_port'] = raw['port']; host.write_text(json.dumps(body))
    else: raw['private_value'] = 'HIDDEN'
    path.write_text(json.dumps(raw))
    with pytest.raises(SupervisorError) as error:
        load_profile(path)
    assert str(error.value) == str(SupervisorError()) and error.value.__suppress_context__


@pytest.mark.asyncio
@pytest.mark.parametrize('failed_role', [None, 'agent', 'native'])
async def test_startup_ready_crash_pair_shutdown_and_fresh_restart(tmp_path, monkeypatch, failed_role):
    monkeypatch.setenv('OPENAI_API_KEY', 'synthetic-test-secret')
    path = fixture_profile(tmp_path)
    previous_directory = None
    for attempt in range(2):
        pair = pair_for(path)
        stopped = asyncio.Event()
        task = asyncio.create_task(pair.run(stopped))
        try:
            await asyncio.wait_for(pair.ready.wait(), 4)
            assert pair.runtime_dir != previous_directory
            previous_directory = pair.runtime_dir
            assert pair.runtime_dir.stat().st_mode & 0o077 == 0
            host = json.loads((pair.runtime_dir/'host.json').read_text())
            endpoint = json.loads((pair.runtime_dir/'endpoint.json').read_text())
            assert host['ipc_socket'] == endpoint['ipc_socket'] == str(pair.runtime_dir/'agent.sock')
            for role in ['agent', 'native']:
                observed = json.loads((tmp_path/(role+'.json')).read_text())
                assert observed['provider_key'] is (role == 'agent')
                assert not observed['parent_env'] and not observed['inheritable']
            if failed_role is None:
                stopped.set()
                await asyncio.wait_for(task, 4)
            else:
                pair.children[0 if failed_role == 'agent' else 1].kill()
                with pytest.raises(SupervisorError):
                    await asyncio.wait_for(task, 4)
        finally:
            stopped.set()
            await asyncio.gather(task, return_exceptions=True)
        assert all(child.returncode is not None for child in pair.children)
        assert not pair.runtime_dir.exists()


@pytest.mark.asyncio
@pytest.mark.parametrize('role', ['agent', 'native'])
async def test_startup_deadline_retires_every_started_child(tmp_path, role):
    pair = pair_for(fixture_profile(tmp_path, {'hang': role}))
    pair.startup_timeout = 0.5
    with pytest.raises(TimeoutError):
        await asyncio.wait_for(pair.run(asyncio.Event()), 4)
    assert all(child.returncode is not None for child in pair.children)
    assert not pair.runtime_dir.exists()
    assert len(pair.children) == (1 if role == 'agent' else 2)


@pytest.mark.asyncio
async def test_stuck_child_is_killed_after_shutdown_bound(tmp_path):
    pair = pair_for(fixture_profile(tmp_path, {'ignore_stop': 'agent'}))
    stopped = asyncio.Event()
    task = asyncio.create_task(pair.run(stopped))
    await asyncio.wait_for(pair.ready.wait(), 4)
    stopped.set()
    await asyncio.wait_for(task, 4)
    assert pair.children[0].returncode == -signal.SIGKILL
    assert pair.children[1].returncode == 0


@pytest.mark.asyncio
async def test_first_child_death_interrupts_second_child_startup(tmp_path):
    pair = pair_for(fixture_profile(tmp_path, {'hang': 'native'}))
    pair.startup_timeout = 60
    stopped = asyncio.Event()
    task = asyncio.create_task(pair.run(stopped))
    try:
        async with asyncio.timeout(4):
            while not (tmp_path/'native.json').exists():
                await asyncio.sleep(0.01)
        pair.children[0].kill()
        with pytest.raises(SupervisorError):
            await asyncio.wait_for(asyncio.shield(task), 1)
        assert all(child.returncode is not None for child in pair.children)
    finally:
        stopped.set()
        await asyncio.gather(task, return_exceptions=True)


@pytest.mark.asyncio
async def test_duplicate_profile_cannot_start_another_pair(tmp_path):
    import fcntl
    path = fixture_profile(tmp_path)
    with path.open('rb') as locked:
        fcntl.flock(locked, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with pytest.raises(BlockingIOError):
            await serve(path)


@pytest.mark.asyncio
async def test_supervisor_sigkill_does_not_leave_children_running(tmp_path):
    path = fixture_profile(tmp_path)
    launcher = tmp_path/'launch.py'
    launcher.write_text('''import asyncio,sys
from pathlib import Path
from doodad_agent.moq_supervisor import PairSupervisor,load_profile
async def main():
 p,h=load_profile(Path(sys.argv[1]))
 await PairSupervisor(p,h,agent_command=[sys.executable,p['endpoint_binary']]).run(asyncio.Event())
asyncio.run(main())
''')
    parent = await asyncio.create_subprocess_exec(sys.executable, str(launcher), str(path),
                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    pids = []
    try:
        async with asyncio.timeout(5):
            assert b'supervised pair ready' in await parent.stdout.readline()
        pids = [json.loads((tmp_path/(role+'.json')).read_text())['pid'] for role in ('agent', 'native')]
        parent.kill(); await parent.wait()
        async with asyncio.timeout(5):
            while pids:
                for pid in pids[:]:
                    try: os.kill(pid, 0)
                    except ProcessLookupError: pids.remove(pid)
                await asyncio.sleep(0.02)
    finally:
        if parent.returncode is None: parent.kill(); await parent.wait()
        for pid in pids:
            try: os.kill(pid, signal.SIGKILL)
            except ProcessLookupError: pass
