import asyncio
import importlib.util
import time
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location('moq_udp_impairment', Path(__file__).parents[1]/'tools/moq_udp_impairment.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
UdpImpairment = module.UdpImpairment


class Receiver(asyncio.DatagramProtocol):
    def __init__(self): self.received = asyncio.Queue()
    def connection_made(self, transport): self.transport = transport
    def datagram_received(self, data, address): self.received.put_nowait((data, address))


@pytest.mark.asyncio
async def test_proxy_preserves_datagrams_and_delays_each_direction():
    loop = asyncio.get_running_loop()
    server, incoming = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    client, replies = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    proxy = UdpImpairment(0, 60, 1)
    try:
        address = await proxy.start(('127.0.0.1', 0), server.get_extra_info('sockname'))
        start = loop.time()
        client.sendto(b'quic-packet-fixture', address)
        data, sender = await asyncio.wait_for(incoming.received.get(), 1)
        assert loop.time()-start >= .025
        server.sendto(data, sender)
        response, _ = await asyncio.wait_for(replies.received.get(), 1)
        assert response == b'quic-packet-fixture' and loop.time()-start >= .055
        assert all(v['forwarded'] == 1 and v['dropped'] == 0 for v in proxy.stats.values())
    finally:
        proxy.close(); server.close(); client.close()


@pytest.mark.asyncio
async def test_seeded_loss_is_counted_and_close_discards_all_pending_payloads():
    class Transport:
        def __init__(self): self.sent = []
        def sendto(self, data, target): self.sent.append(data)
    a, b = UdpImpairment(5, 120, 44), UdpImpairment(5, 120, 44)
    outputs = [Transport(), Transport()]
    for proxy, output in zip((a, b), outputs):
        for index in range(100):
            proxy._schedule('uplink', bytes([index]), output, None, proxy.random[0])
    assert a.stats == b.stats and 0 < a.stats['uplink']['dropped'] < 100
    await asyncio.sleep(.08)
    assert outputs[0].sent == outputs[1].sent
    assert a.stats['uplink']['forwarded'] + a.stats['uplink']['dropped'] == 100
    for proxy in (a, b):
        proxy._schedule('uplink', b'pending', outputs[0], None, proxy.random[0])
        proxy.close()
        assert not proxy.pending
    count = len(outputs[0].sent)
    await asyncio.sleep(.08)
    assert len(outputs[0].sent) == count


@pytest.mark.asyncio
async def test_proxy_memory_pressure_is_explicit_and_bounded():
    proxy = UdpImpairment(0, 120, 1)
    for _ in range(proxy.MAX_PENDING + 1):
        proxy._schedule('uplink', b'fixture', None, None, proxy.random[0])
    assert len(proxy.pending) == proxy.MAX_PENDING
    assert proxy.stats['uplink']['pressure'] == 1
    proxy.close()


@pytest.mark.asyncio
async def test_proxy_reports_unrequested_event_loop_delay():
    class Transport:
        def sendto(self, data, target): pass
    loop=asyncio.get_running_loop()
    proxy=UdpImpairment(0,60,1)
    try:
        # Deliberately block the loop, rather than adding network delay. The
        # fixture must expose this extra delay independently of requested RTT.
        proxy._heartbeat(loop)
        proxy._schedule('uplink',b'fixture',Transport(),None,proxy.random[0])
        time.sleep(.15)
        await asyncio.sleep(.01)
        result=proxy.snapshot()
        assert result['timing']['uplink']['max_forward_lateness_us']>=100_000
        assert result['timing']['uplink']['late_over_100ms']==1
        assert result['event_loop_max_lag_us']>=100_000
        assert result['directions']['uplink']['forwarded']==1
    finally:
        proxy.close()
    assert proxy.heartbeat is None and not proxy.pending


@pytest.mark.asyncio
async def test_bounded_outage_drops_only_selected_direction_and_expires():
    class Transport:
        def sendto(self, data, target): pass
    proxy=UdpImpairment(0,0,1)
    try:
        proxy.blackout('uplink',50)
        proxy._schedule('uplink',b'fixture',Transport(),None,proxy.random[0])
        proxy._schedule('downlink',b'fixture',Transport(),None,proxy.random[1])
        await asyncio.sleep(.07)
        proxy._schedule('uplink',b'fixture',Transport(),None,proxy.random[0])
        await asyncio.sleep(.01)
        result=proxy.snapshot()
        assert result['outage_dropped']=={'uplink':1,'downlink':0}
        assert result['directions']['uplink']['forwarded']==1
        assert result['directions']['downlink']['forwarded']==1
        for direction,duration in [('both',50),('uplink',0),('uplink',2001)]:
            with pytest.raises(ValueError): proxy.blackout(direction,duration)
    finally:
        proxy.close()


@pytest.mark.asyncio
async def test_reordering_delivers_original_datagrams_out_of_order_in_both_directions():
    loop = asyncio.get_running_loop()
    server, incoming = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    client, replies = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    proxy = None
    try:
        proxy = UdpImpairment(0, 0, 44, reorder_every=2, reorder_delay_ms=40)
        address = await proxy.start(('127.0.0.1', 0), server.get_extra_info('sockname'))
        for payload in (b'a', b'b', b'c'): client.sendto(payload, address)
        upstream = [await asyncio.wait_for(incoming.received.get(), 1) for _ in range(3)]
        assert [data for data, _ in upstream] == [b'a', b'c', b'b']
        for payload in (b'd', b'e', b'f'): server.sendto(payload, upstream[0][1])
        downstream = [await asyncio.wait_for(replies.received.get(), 1) for _ in range(3)]
        assert [data for data, _ in downstream] == [b'd', b'f', b'e']
        assert all(row['reordered'] == 1 and row['reorder_scheduled'] == 1
                   for row in proxy.snapshot()['packet_faults'].values())
        assert all(row['dropped'] == 0 and row['forwarded'] == 3 for row in proxy.stats.values())
    finally:
        if proxy: proxy.close()
        server.close(); client.close()


@pytest.mark.asyncio
async def test_duplicate_is_byte_exact_and_counted_separately_in_both_directions():
    loop = asyncio.get_running_loop()
    server, incoming = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    client, replies = await loop.create_datagram_endpoint(Receiver, local_addr=('127.0.0.1', 0))
    proxy = None
    try:
        proxy = UdpImpairment(0, 0, 44, duplicate_every=2, duplicate_delay_ms=5)
        address = await proxy.start(('127.0.0.1', 0), server.get_extra_info('sockname'))
        for payload in (b'first', b'\x00\xff\x01', b'last'): client.sendto(payload, address)
        upstream = [await asyncio.wait_for(incoming.received.get(), 1) for _ in range(4)]
        assert sorted(data for data, _ in upstream) == sorted([b'first', b'\x00\xff\x01', b'last', b'\x00\xff\x01'])
        for payload in (b'one', b'\xff\x00', b'three'): server.sendto(payload, upstream[0][1])
        downstream = [await asyncio.wait_for(replies.received.get(), 1) for _ in range(4)]
        assert sorted(data for data, _ in downstream) == sorted([b'one', b'\xff\x00', b'three', b'\xff\x00'])
        assert all(row['duplicated'] == 1 and row['duplicate_scheduled'] == 1
                   for row in proxy.snapshot()['packet_faults'].values())
        assert all(row['received'] == 3 and row['forwarded'] == 4 for row in proxy.stats.values())
    finally:
        if proxy: proxy.close()
        server.close(); client.close()


@pytest.mark.asyncio
async def test_duplicate_reservation_shares_the_original_pending_bound_and_close_discards_both():
    class Transport:
        def __init__(self): self.sent = []
        def sendto(self, data, target): self.sent.append((data, target))
    proxy = UdpImpairment(0, 120, 44, duplicate_every=1, duplicate_delay_ms=20)
    output = Transport()
    for _ in range(proxy.MAX_PENDING // 2 + 1):
        proxy._schedule('downlink', b'payload', output, ('127.0.0.1', 1234), proxy.random[1])
    assert len(proxy.pending) == proxy.MAX_PENDING
    assert proxy.stats['downlink']['pressure'] == 1
    assert proxy.snapshot()['packet_faults']['downlink']['duplicate_scheduled'] == proxy.MAX_PENDING // 2
    proxy.close()
    await asyncio.sleep(.1)
    assert not proxy.pending and not output.sent


@pytest.mark.asyncio
async def test_delayed_original_and_duplicate_keep_their_original_client_port():
    class Transport:
        def __init__(self): self.sent = []
        def sendto(self, data, target): self.sent.append((data, target))
    output = Transport()
    proxy = UdpImpairment(0, 60, 44, duplicate_every=1, duplicate_delay_ms=5)
    proxy.transports = [None, output]
    try:
        proxy.client = ('127.0.0.1', 1234)
        proxy._downlink(b'old-connection', None)
        proxy.client = ('127.0.0.1', 5678)
        proxy._downlink(b'new-connection', None)
        await asyncio.sleep(.06)
        assert len(output.sent) == 4
        assert all(target == ('127.0.0.1', 1234 if data == b'old-connection' else 5678)
                   for data, target in output.sent)
    finally:
        proxy.transports = []
        proxy.close()


@pytest.mark.parametrize('options', [
    {'reorder_every': 2}, {'reorder_delay_ms': 40},
    {'duplicate_every': True, 'duplicate_delay_ms': 5},
    {'duplicate_every': 2, 'duplicate_delay_ms': 501},
    {'reorder_every': -1, 'reorder_delay_ms': 40},
])
def test_packet_fault_configuration_rejects_unbounded_or_partial_profiles(options):
    with pytest.raises(ValueError): UdpImpairment(0, 0, 44, **options)
