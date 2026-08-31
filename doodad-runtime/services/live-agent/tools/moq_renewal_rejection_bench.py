#!/usr/bin/env python3
"""Physical negative renewal test using a private, deliberately faulty host.

Corrupt exactly one fresh renewal time MAC after authenticating the watch's
proof. Require the real native acknowledgment, then firmware rejection at its
renewal-verification site, with no accepted renewal or microphone samples.
Does not flash or modify a permanent host. Reapply permanent enrollment afterward.
"""
import argparse
import asyncio
import json
from pathlib import Path
import re

import moq_ultra_bench as bench
from doodad_agent.moq_session import MoqSession


async def run(args):
    observations = {'time_macs_corrupted': 0, 'native_renewal_acknowledgments': 0}
    registry_type, native, stop = bench.GrantRegistry, MoqSession.native, bench.stop
    drained = False

    async def drain_serial_then_stop(process):
        nonlocal drained
        if not drained:
            drained = True
            # A negative case can close WSS before the monitor's 100 ms read
            # returns its last UART bytes. Collect that bounded tail before
            # interrupting the monitor; never infer rejection from EOF alone.
            await asyncio.sleep(.35)
        await stop(process)

    class FaultyRegistry(registry_type):
        def renew(self, *values):
            document = super().renew(*values)
            observations['time_macs_corrupted'] += 1
            proof = document['time']['proof']
            document['time']['proof'] = ('0' if proof[0] != '0' else '1') + proof[1:]
            return document

    async def observed_native(self, peer, packet):
        await native(self, peer, packet)
        if packet.header['type'] == 'session.renewed':
            observations['native_renewal_acknowledgments'] += 1

    options = argparse.Namespace(**vars(args), audio=False, voice_ui=False, capture_ms=1200,
        capture_rounds=1, reply_each_capture=False, capture_outage_ms=0, loss_percent=0,
        added_rtt_ms=0, loss_seed=44, max_playout_pressure=None, max_quic_heap_bytes=None,
        certificate_fault=None, long_response_seconds=0)
    expected_failure = False
    bench.GrantRegistry, MoqSession.native, bench.stop = FaultyRegistry, observed_native, drain_serial_then_stop
    try:
        try:
            await bench.run(options)
        except RuntimeError:
            expected_failure = True
    finally:
        bench.GrantRegistry, MoqSession.native, bench.stop = registry_type, native, stop
    result = json.loads((args.output/'result.json').read_text())
    serial = (args.output/'serial.log').read_text(errors='replace')
    source = (bench.ROOT/'doodad-runtime/firmware/main/src/voice_service.cpp').read_text().splitlines()
    sites = [number for number, line in enumerate(source, 1)
             if '!secure::commit_renewal(*g_grant,renewed)) retire_control();' in line]
    rejected = len(sites) == 1 and re.search(r'MoQ control retired site='+str(sites[0])+r'\b', serial) is not None
    observations.update(firmware_rejection_site=sites[0] if len(sites)==1 else None,
        firmware_rejected=rejected, accepted_renewals=serial.count('MoQ authorization renewed revision='),
        microphone_samples=result['microphone_samples'], ready_sessions=result['ready_sessions'])
    observations['pass'] = (expected_failure and not result['pass_'] and rejected
        and observations['time_macs_corrupted']==1 and observations['native_renewal_acknowledgments']==1
        and observations['accepted_renewals']==0 and result['microphone_samples']==0
        and result['ready_sessions']==2)
    bench.write(args.output/'renewal-rejection.json',observations)
    if not observations['pass']:
        raise RuntimeError('renewal rejection gate failed')
    print(json.dumps(observations),flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--host',required=True)
    parser.add_argument('--port',required=True)
    parser.add_argument('--idf-python',type=Path,required=True)
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit('Renewal rejection bench failed ('+type(error).__name__+'); inspect private evidence') from None


if __name__ == '__main__':
    main()
