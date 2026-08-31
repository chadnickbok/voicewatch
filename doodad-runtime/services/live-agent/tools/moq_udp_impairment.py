"""Bounded, single-watch UDP impairment fixture; never a production relay.

Adds seeded loss/delay and optional deterministic reordering/duplication.
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

    def __init__(self, loss_percent, rtt_ms, seed, *, reorder_every=0,
                 reorder_delay_ms=0, duplicate_every=0, duplicate_delay_ms=0):
        if loss_percent not in {0, 1, 3, 5} or rtt_ms not in {0, 30, 60, 120}:
            raise ValueError('unsupported impairment cell')
        for every, delay in ((reorder_every, reorder_delay_ms), (duplicate_every, duplicate_delay_ms)):
            if (type(every) is not int or type(delay) is not int
                    or not 0 <= every <= 1000 or not 0 <= delay <= 500
                    or bool(every) != bool(delay)):
                raise ValueError('unsupported packet fault profile')
        self.loss_percent, self.rtt_ms, self.seed = loss_percent, rtt_ms, seed
        self.reorder_every, self.reorder_delay_ms = reorder_every, reorder_delay_ms
        self.duplicate_every, self.duplicate_delay_ms = duplicate_every, duplicate_delay_ms
        self.random = [random.Random(seed), random.Random(seed ^ 0x937)]
        self.transports = []
        self.client = None
        self.pending = set()
        self.closed = False
        self.stats = {d: dict(received=0, dropped=0, forwarded=0, pressure=0) for d in ['uplink', 'downlink']}
        self.timing = {d: dict(max_forward_lateness_us=0, late_over_20ms=0, late_over_100ms=0)
                       for d in ['uplink','downlink']}
        self.loop_max_lag_us = 0
        self.heartbeat = None
        self.high_water = 0
        self.outage_until = {d: 0.0 for d in self.stats}
        self.outage_dropped = {d: 0 for d in self.stats}
        self.packet_faults = {d: dict(reorder_scheduled=0, reordered=0,
                                      duplicate_scheduled=0, duplicated=0) for d in self.stats}
        self.highest_forwarded = {d: 0 for d in self.stats}

    def blackout(self, direction, duration_ms):
        """Drop new datagrams for one bounded interval; never touch WSS."""
        if self.closed or direction not in self.stats or type(duration_ms) is not int or not 1 <= duration_ms <= 2000:
            raise ValueError('unsupported media outage')
        self.outage_until[direction] = asyncio.get_running_loop().time() + duration_ms / 1000

    async def start(self, listen, backend):
        loop = asyncio.get_running_loop()
        upstream, _ = await loop.create_datagram_endpoint(
            lambda: _Receiver(self._downlink), remote_addr=backend)
        self.transports.append(upstream)
        try:
            frontend, _ = await loop.create_datagram_endpoint(
                lambda: _Receiver(self._uplink), local_addr=listen)
            self.transports.append(frontend)
            self._heartbeat(loop)
            return frontend.get_extra_info('sockname')
        except BaseException:
            self.close()
            raise

    def _heartbeat(self, loop):
        due = loop.time()+.01
        def tick():
            self.loop_max_lag_us=max(self.loop_max_lag_us,round(max(0,loop.time()-due)*1_000_000))
            if not self.closed: self._heartbeat(loop)
        self.heartbeat=loop.call_at(due,tick)

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
        sequence = stats['received']
        duplicate = bool(self.duplicate_every and sequence % self.duplicate_every == 0)
        # Original and duplicate reserve from the same bound. A fixture that
        # runs out of room reports pressure; it cannot silently under-inject.
        if len(data) > self.MAX_DATAGRAM or len(self.pending) + 1 + duplicate > self.MAX_PENDING:
            stats['pressure'] += 1
            return
        if asyncio.get_running_loop().time() < self.outage_until[direction]:
            self.outage_dropped[direction] += 1
            stats['dropped'] += 1
            return
        if rng.random() * 100 < self.loss_percent:
            stats['dropped'] += 1
            return
        loop=asyncio.get_running_loop()
        due=loop.time()+self.rtt_ms/2000
        faults = self.packet_faults[direction]
        if self.reorder_every and sequence % self.reorder_every == 0:
            due += self.reorder_delay_ms/1000
            faults['reorder_scheduled'] += 1
        self._queue(direction, data, transport, target, due, sequence, duplicate=False)
        if duplicate:
            faults['duplicate_scheduled'] += 1
            self._queue(direction, data, transport, target,
                        due+self.duplicate_delay_ms/1000, sequence, duplicate=True)

    def _queue(self, direction, data, transport, target, due, sequence, *, duplicate):
        loop = asyncio.get_running_loop()
        stats = self.stats[direction]
        def forward():
            self.pending.discard(handle)
            if not self.closed:
                late=round(max(0,loop.time()-due)*1_000_000)
                timing=self.timing[direction]
                timing['max_forward_lateness_us']=max(timing['max_forward_lateness_us'],late)
                timing['late_over_20ms']+=int(late>20_000)
                timing['late_over_100ms']+=int(late>100_000)
                transport.sendto(data, target)
                stats['forwarded'] += 1
                if duplicate:
                    self.packet_faults[direction]['duplicated'] += 1
                else:
                    if sequence < self.highest_forwarded[direction]:
                        self.packet_faults[direction]['reordered'] += 1
                    self.highest_forwarded[direction] = max(sequence, self.highest_forwarded[direction])
        handle = loop.call_at(due, forward)
        self.pending.add(handle)
        self.high_water = max(self.high_water, len(self.pending))

    def close(self):
        self.closed = True
        if self.heartbeat: self.heartbeat.cancel(); self.heartbeat=None
        for handle in self.pending: handle.cancel()
        self.pending.clear()
        for transport in self.transports: transport.close()

    def snapshot(self):
        return dict(loss_percent=self.loss_percent, added_rtt_ms=self.rtt_ms,
                    seed=self.seed, directions=self.stats, pending=len(self.pending),
                    pending_high_water=self.high_water, closed=self.closed,
                    timing=self.timing, event_loop_max_lag_us=self.loop_max_lag_us,
                    outage_dropped=self.outage_dropped,
                    reorder_every=self.reorder_every, reorder_delay_ms=self.reorder_delay_ms,
                    duplicate_every=self.duplicate_every, duplicate_delay_ms=self.duplicate_delay_ms,
                    packet_faults=self.packet_faults)
