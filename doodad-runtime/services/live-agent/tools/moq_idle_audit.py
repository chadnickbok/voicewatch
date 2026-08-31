#!/usr/bin/env python3
"""Read-only audit of completed idle fixtures; never emits raw logs or identities.

Protocol success, sampled resource invariants and cumulative heap recovery are
separate conclusions. RSS and free-heap differences are observations, not an
allocation attribution or a whole-product leak-free verdict.
"""
import argparse
import json
from pathlib import Path
import re

from moq_idle_soak import FAULTS, STATUS_FIELDS, statuses, validate_idle_status


RESOURCE_FIELDS = ('internal_free', 'internal_min', 'internal_largest',
                   'psram_free', 'audio_stack', 'network_stack', 'dns_stack',
                   'native_rss_kib', 'service_rss_kib')
HEAP_FIELDS = ('live', 'peak', 'limit', 'blocks', 'allocations', 'frees', 'denied', 'failures')
# The monitor requests status every five seconds and the host samples every
# thirty. These audit bounds allow the planned <=10 s reconnect plus settling,
# but refuse to infer continuous coverage from only start/end observations.
MAX_SERIAL_GAP_MS = 20000
MAX_HOST_GAP_MS = 60000


def heap_rows(raw, tls):
    fields = ('valid', *HEAP_FIELDS) if tls else HEAP_FIELDS
    pattern = ('TLS' if tls else 'QUIC') + ' heap ' + ' '.join(key + r'=(\d+)' for key in fields)
    return [dict(zip(fields, map(int, values)))
            for values in re.findall(pattern + r'(?:\r?\n|\x1b\[[0-9;]*m)', raw)]


def envelope(rows, field):
    values = [row[field] for row in rows]
    return dict(first=values[0], last=values[-1], minimum=min(values), maximum=max(values),
                last_minus_first=values[-1] - values[0])


def audit(result, raw, events, recovery):
    findings = []

    def require(condition, code):
        if not condition and code not in findings:
            findings.append(code)

    state = result.get('idle', {})
    seconds = state.get('requested_seconds')
    require(type(seconds) is int and seconds in (120, 28800), 'unsupported_duration')
    if findings:
        return dict(operational_checks_pass=False, findings=findings,
                    eight_hour_protocol_verified=False, eight_hour_gate_complete=False)
    interval = 60 if seconds == 120 else 3600
    count = (seconds - 1) // interval
    reconnects = state.get('reconnects', [])
    snapshots = state.get('snapshots', [])
    rows = statuses(raw)
    require(result.get('pass_') is True and state.get('protocol_pass') is True, 'runner_failed')
    require(result.get('microphone_samples') == 0 and state.get('microphone_opened') is False,
            'unexpected_microphone')
    require(result.get('idle_monitor_survived') is True, 'monitor_did_not_survive')
    require(not any(marker in raw for marker in FAULTS), 'firmware_fault')
    require(state.get('elapsed_ms', 0) >= seconds * 1000, 'duration_incomplete')
    starts = [event for event in events if event.get('kind') == 'idle_soak_started']
    ends = [event for event in events if event.get('kind') == 'idle_soak_finished']
    require(len(starts) == len(ends) == 1, 'missing_phase_events')
    if len(starts) == len(ends) == 1:
        require(starts[0].get('seconds') == seconds and
                ends[0].get('elapsed_ms', 0) - starts[0].get('elapsed_ms', 0) >= seconds * 1000 and
                ends[0].get('idle_elapsed_ms') == state.get('elapsed_ms'), 'event_duration_mismatch')
    require(len(reconnects) == count, 'reconnect_count')
    for index, reconnect in enumerate(reconnects):
        at, duration = reconnect.get('at_ms', -1), reconnect.get('duration_ms', -1)
        require(0 <= duration <= 10000, 'reconnect_too_slow')
        require((index + 1) * interval * 1000 <= at < (index + 2) * interval * 1000,
                'reconnect_schedule')
    renewals = state.get('renewals_per_session', [])
    require(len(renewals) == count + 1 and all(type(n) is int and n > 0 for n in renewals),
            'renewal_coverage')
    require(result.get('ready_sessions') == count + 2 and
            0 <= result.get('forced_reconnect_ms', -1) <= 10000 and
            result.get('renewals_completed', 0) >= 2, 'final_reconnect_coverage')
    require(recovery.get('fresh_permanent_ready_observed') is True and
            recovery.get('persistent_services_alive') is True, 'permanent_recovery_unverified')

    require(len(rows) >= 2, 'missing_serial_snapshots')
    for row in rows:
        try:
            validate_idle_status(row)
        except RuntimeError:
            require(False, 'serial_resource_invariant')
    require(all(right['uptime_ms'] > left['uptime_ms'] for left, right in zip(rows, rows[1:])),
            'firmware_clock_regressed')
    serial_gap = max((b['uptime_ms'] - a['uptime_ms'] for a, b in zip(rows, rows[1:])), default=0)
    require(serial_gap <= MAX_SERIAL_GAP_MS, 'serial_observation_gap')
    required = (*STATUS_FIELDS, 'elapsed_ms', 'native_rss_kib', 'service_rss_kib')
    valid_snapshots = all(all(type(row.get(key)) is int and row[key] >= 0 for key in required)
                          for row in snapshots)
    require(bool(snapshots) and valid_snapshots, 'missing_host_resource_snapshots')
    epochs = []
    if snapshots and valid_snapshots:
        observed = {tuple(row[key] for key in STATUS_FIELDS) for row in rows}
        require(all(tuple(row[key] for key in STATUS_FIELDS) in observed for row in snapshots),
                'snapshot_not_in_serial')
        require(all(right['elapsed_ms'] > left['elapsed_ms'] and right['uptime_ms'] > left['uptime_ms']
                    for left, right in zip(snapshots, snapshots[1:])), 'snapshot_clock_regressed')
        require(snapshots[-1]['elapsed_ms'] >= seconds * 1000, 'snapshot_duration_incomplete')
        require(snapshots[0]['elapsed_ms'] <= 15000 and all(
            b['elapsed_ms'] - a['elapsed_ms'] <= MAX_HOST_GAP_MS for a, b in zip(snapshots, snapshots[1:])),
            'host_observation_gap')
        boundaries = [0, *(r.get('at_ms', -1) for r in reconnects), state.get('elapsed_ms', 0) + 1]
        for index, (begin, end) in enumerate(zip(boundaries, boundaries[1:])):
            selected = [row for row in snapshots if begin <= row['elapsed_ms'] < end]
            require(bool(selected), 'missing_session_resource_snapshot')
            if selected:
                epochs.append(dict(index=index, samples=len(selected),
                    first_elapsed_ms=selected[0]['elapsed_ms'], last_elapsed_ms=selected[-1]['elapsed_ms'],
                    resources={field: envelope(selected, field) for field in RESOURCE_FIELDS}))

    heaps = {}
    for tls, name, limit in ((False, 'quic', 131072), (True, 'tls', 262144)):
        values = heap_rows(raw, tls)
        require(bool(values), name + '_missing_heap_snapshots')
        for row in values:
            require((not tls or row['valid'] == 1) and row['limit'] == limit and
                    row['live'] <= row['peak'] <= limit and row['denied'] == row['failures'] == 0 and
                    row['allocations'] - row['frees'] == row['blocks'], name + '_heap_invariant')
        heaps[name] = dict(samples=len(values), peak=max((r['peak'] for r in values), default=0),
                           final_live=values[-1]['live'] if values else None)

    deltas = {}
    if epochs:
        baseline = epochs[0]['resources']
        # Compare each session's best observed free heap and lowest RSS. Keep
        # every epoch visible; a final return must not hide intermediate loss.
        for field in ('internal_free', 'psram_free', 'native_rss_kib', 'service_rss_kib'):
            edge = 'minimum' if field.endswith('_rss_kib') else 'maximum'
            deltas[field] = [epoch['resources'][field][edge] - baseline[field][edge] for epoch in epochs]
    return dict(requested_seconds=seconds, elapsed_ms=state.get('elapsed_ms', 0),
        operational_checks_pass=not findings, findings=findings,
        eight_hour_protocol_verified=seconds == 28800 and not findings,
        serial_snapshots=len(rows), host_snapshots=len(snapshots),
        maximum_serial_gap_ms=serial_gap,
        reconnects=[{key: r.get(key) for key in ('at_ms', 'duration_ms')} for r in reconnects],
        renewals_per_session=renewals, epochs=epochs, session_baseline_deltas=deltas, heaps=heaps,
        cumulative_heap_recovery_verified=False, eight_hour_gate_complete=False,
        full_replacement_ready=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--recovery', type=Path, required=True)
    args = parser.parse_args()
    report = audit(json.loads((args.directory / 'result.json').read_text()),
                   (args.directory / 'serial.log').read_text(errors='replace'),
                   [json.loads(line) for line in (args.directory / 'events.jsonl').read_text().splitlines()],
                   json.loads(args.recovery.read_text()))
    print(json.dumps(report, indent=2))
    if not report['operational_checks_pass']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
