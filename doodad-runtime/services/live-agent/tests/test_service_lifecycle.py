"""A failed discovery startup must not leave a live control listener behind."""
import asyncio
import json

import pytest

from doodad_agent import main, transport_webrtc


@pytest.mark.asyncio
@pytest.mark.parametrize('stage', ['discovery', 'transport'])
async def test_startup_failure_closes_transport_and_store(monkeypatch, tmp_path, stage):
    events = []

    class Transport:
        def __init__(self, *args, **kwargs): pass
        async def start(self):
            events.append('transport.start')
            if stage == 'transport':
                raise RuntimeError('transport startup rejected')
        async def close(self): events.append('transport.close')

    class Store:
        def __init__(self, *args): pass
        def close(self): events.append('store.close')

    def fail(*args):
        raise RuntimeError('discovery registration rejected')

    monkeypatch.setattr(transport_webrtc, 'WatchTransportServer', Transport)
    monkeypatch.setattr(main, 'Store', Store)
    monkeypatch.setattr(main, 'advertise', fail)
    monkeypatch.setattr(main, 'local_ipv4', lambda: '127.0.0.1')
    monkeypatch.setattr(main, 'personal_trust_from_environment', lambda: None)
    args = main.parse_arguments(['serve', '--database', str(tmp_path/'db'),
                                 '--trace', str(tmp_path/'trace.jsonl')])
    with pytest.raises(RuntimeError, match='rejected'):
        await main.serve(args)
    assert events == ['transport.start', 'transport.close', 'store.close']


@pytest.mark.asyncio
@pytest.mark.parametrize('cancel', [False, True])
async def test_failed_registration_closes_discovery_owner(monkeypatch, cancel):
    events = []
    entered = asyncio.Event()

    class Discovery:
        def __init__(self, **kwargs): pass
        async def async_register_service(self, service):
            events.append('register')
            entered.set()
            if cancel:
                await asyncio.Event().wait()
            raise RuntimeError('duplicate name')
        async def async_close(self): events.append('close')

    monkeypatch.setattr(main, 'AsyncZeroconf', Discovery)
    task = asyncio.create_task(main.advertise('127.0.0.1', 8080))
    await entered.wait()
    if cancel:
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
    else:
        with pytest.raises(RuntimeError, match='duplicate name'):
            await task
    assert events == ['register', 'close']


@pytest.mark.asyncio
async def test_discovery_readiness_waits_for_registration_announcement(monkeypatch):
    events = []

    class Discovery:
        def __init__(self, **kwargs): pass
        async def async_register_service(self, service):
            events.append('register')
            async def announce():
                await asyncio.sleep(0)
                events.append('announced')
            return asyncio.create_task(announce())
        async def async_close(self): events.append('close')

    monkeypatch.setattr(main, 'AsyncZeroconf', Discovery)
    owner, service = await main.advertise('127.0.0.1', 8080, 'moq')
    assert events == ['register', 'announced']
    assert service.properties[b'transport'] == b'moq-lite-05'
    assert service.properties[b'tls'] == b'1'
    assert service.name == 'Doodad MoQ Live Agent (127.0.0.1:8080)._doodad-voice._tcp.local.'
    other, other_service = await main.advertise('127.0.0.1', 8081, 'moq')
    assert other_service.name != service.name
    await other.async_close()
    await owner.async_close()
    assert events == ['register', 'announced', 'register', 'announced', 'close', 'close']


@pytest.mark.asyncio
async def test_explicit_endpoint_service_reaches_ready_and_closes_without_discovery(monkeypatch, tmp_path):
    events = []

    class Transport:
        def __init__(self, *args, **kwargs): pass
        async def start(self): events.append('transport.start')
        async def close(self): events.append('transport.close')

    class Store:
        def __init__(self, *args): pass
        def close(self): events.append('store.close')

    def unexpected(*args): raise AssertionError('discovery must stay disabled')

    monkeypatch.setattr(transport_webrtc, 'WatchTransportServer', Transport)
    monkeypatch.setattr(main, 'Store', Store)
    monkeypatch.setattr(main, 'advertise', unexpected)
    monkeypatch.setattr(main, 'local_ipv4', lambda: '127.0.0.1')
    monkeypatch.setattr(main, 'personal_trust_from_environment', lambda: None)
    loop = asyncio.get_running_loop()
    monkeypatch.setattr(loop, 'add_signal_handler', lambda *args: None)
    monkeypatch.setattr(loop, 'remove_signal_handler', lambda *args: None)
    trace = tmp_path/'trace.jsonl'
    args = main.parse_arguments(['serve', '--no-discovery', '--database', str(tmp_path/'db'),
                                 '--trace', str(trace)])
    task = asyncio.create_task(main.serve(args))
    await asyncio.sleep(0)
    rows = [json.loads(line) for line in trace.read_text().splitlines()]
    assert any(row['kind'] == 'service.ready' and row['discovery'] is False for row in rows)
    assert not task.done()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert events == ['transport.start', 'transport.close', 'store.close']
    assert json.loads(trace.read_text().splitlines()[-1])['kind'] == 'shutdown.completed'
