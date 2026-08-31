"""Bounded, single-watch UDP impairment fixture; never a production relay.

Adds seeded independent packet loss and one-way delay to both directions.
It cannot decrypt QUIC and does not alter WSS, certificates, ALPN or grants.
Pending packets are held only in RAM and discarded on close.
"""
import asyncio
import random


class _Receiver(asyncio.DatagramProtocol):
    def __init__(self, callback): self.callback = callback
    def datagram_received(self, data, address): self.callback(data, address)


class UdpImpairment:
    MAX_PENDING = 256
    MAX_DATAGRAM = 2048

    def __init__(self, loss_percent, rtt_ms, seed):
        if loss_percent not in {0, 1, 3, 5} or rtt_ms not in {0, 30, 60, 120}:
            raise ValueError('unsupported impairment cell')
        self.loss_percent, self.rtt_ms, self.seed = loss_percent, rtt_ms, seed
        self.random = [random.Random(seed), random.Random(seed ^ 0x937)]
        self.transports = []
        self.client = None
        self.pending = set()
        self.closed = False
        self.stats = {d: dict(received=0, dropped=0, forwarded=0, pressure=0) for d in ['uplink', 'downlink']}
        self.high_water = 0

    async def start(self, listen, backend):
        loop = asyncio.get_running_loop()
        upstream, _ = await loop.create_datagram_endpoint(
            lambda: _Receiver(self._downlink), remote_addr=backend)
        self.transports.append(upstream)
        try:
            frontend, _ = await loop.create_datagram_endpoint(
                lambda: _Receiver(self._uplink), local_addr=listen)
            self.transports.append(frontend)
            return frontend.get_extra_info('sockname')
        except BaseException:
            self.close()
            raise

    def _uplink(self, data, address):
        if self.client is not None and self.client[0] != address[0]:
            return  # One watch/IP per bench, with new UDP ports on reconnect.
        self.client = address
        self._schedule('uplink', data, self.transports[0], None, self.random[0])

    def _downlink(self, data, address):
        if self.client is not None:
            self._schedule('downlink', data, self.transports[1], self.client, self.random[1])

    def _schedule(self, direction, data, transport, target, rng):
        if self.closed:
            return
        stats = self.stats[direction]
        stats['received'] += 1
        if len(data) > self.MAX_DATAGRAM or len(self.pending) >= self.MAX_PENDING:
            stats['pressure'] += 1
            return
        if rng.random() * 100 < self.loss_percent:
            stats['dropped'] += 1
            return
        def forward():
            self.pending.discard(handle)
            if not self.closed:
                transport.sendto(data, target)
                stats['forwarded'] += 1
        handle = asyncio.get_running_loop().call_later(self.rtt_ms / 2000, forward)
        self.pending.add(handle)
        self.high_water = max(self.high_water, len(self.pending))

    def close(self):
        self.closed = True
        for handle in self.pending: handle.cancel()
        self.pending.clear()
        for transport in self.transports: transport.close()

    def snapshot(self):
        return dict(loss_percent=self.loss_percent, added_rtt_ms=self.rtt_ms,
                    seed=self.seed, directions=self.stats, pending=len(self.pending),
                    pending_high_water=self.high_water, closed=self.closed)
