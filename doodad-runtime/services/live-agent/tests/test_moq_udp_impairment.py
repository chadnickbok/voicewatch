import asyncio
import importlib.util
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
