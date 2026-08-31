import copy
import importlib.util
from pathlib import Path
import sys

import pytest


tools = Path(__file__).parents[1] / 'tools'
spec = importlib.util.spec_from_file_location('moq_idle_audit', tools / 'moq_idle_audit.py')
audit_module = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(tools))
try:
    spec.loader.exec_module(audit_module)
finally:
    sys.path.remove(str(tools))


def fixture(seconds=120):
    interval = 60 if seconds == 120 else 3600
    reconnects = [dict(at_ms=at * 1000, duration_ms=6200) for at in range(interval, seconds, interval)]
    snapshots = []
    times = set(range(0, seconds * 1000 + 1, 30000))
    for reconnect in reconnects:
        times.discard(reconnect['at_ms'])
        times.add(reconnect['at_ms'] + 12500)
    for index, at in enumerate(sorted(times)):
        row = {key: 0 for key in audit_module.STATUS_FIELDS}
        row.update(uptime_ms=100000 + at, ready=1, internal_free=164000,
                   internal_min=99000, internal_largest=79000, psram_free=7000000,
                   audio_stack=6000, network_stack=5000, dns_stack=3000,
                   elapsed_ms=at, native_rss_kib=18000, service_rss_kib=60000, renewals=index)
        snapshots.append(row)
    result = dict(pass_=True, microphone_samples=0, idle_monitor_survived=True,
                  ready_sessions=len(reconnects) + 2, forced_reconnect_ms=6200, renewals_completed=2,
                  idle=dict(requested_seconds=seconds, elapsed_ms=seconds * 1000,
                            protocol_pass=True, microphone_opened=False, reconnects=reconnects,
                            renewals_per_session=[2] * (len(reconnects) + 1), snapshots=snapshots))
    events = [dict(kind='idle_soak_started', elapsed_ms=8000, seconds=seconds),
              dict(kind='idle_soak_finished', elapsed_ms=8000 + seconds * 1000,
                   idle_elapsed_ms=seconds * 1000)]
    recovery = dict(fresh_permanent_ready_observed=True, persistent_services_alive=True)
    return result, events, recovery


def serial(result):
    raw = 'private arbitrary application text must not escape\n'
    observed = {row['elapsed_ms']: row for row in result['idle']['snapshots']}
    # Populate the independent five-second monitor stream, preserving each
    # exact host snapshot so membership checks exercise real field matching.
    for at in range(0, result['idle']['snapshots'][-1]['elapsed_ms'] + 1, 5000):
        if at not in observed:
            prior = max(key for key in observed if key < at)
            observed[at] = dict(observed[prior], elapsed_ms=at, uptime_ms=100000 + at)
    for at, row in sorted(observed.items()):
        raw += 'MOQ_STATUS ' + ' '.join(f'{key}={row[key]}' for key in audit_module.STATUS_FIELDS) + '\n'
        raw += 'TLS heap valid=1 live=10 peak=20 limit=262144 blocks=2 allocations=5 frees=3 denied=0 failures=0\n'
        raw += 'QUIC heap live=10 peak=20 limit=131072 blocks=2 allocations=5 frees=3 denied=0 failures=0\n'
    return raw


@pytest.mark.parametrize('seconds', (120, 28800))
def test_duration_scope_and_resource_audit_never_infer_whole_product_or_heap_recovery(seconds):
    result, events, recovery = fixture(seconds)
    report = audit_module.audit(result, serial(result), events, recovery)
    assert report['operational_checks_pass'] and not report['findings']
    assert report['eight_hour_protocol_verified'] == (seconds == 28800)
    assert not report['eight_hour_gate_complete'] and not report['cumulative_heap_recovery_verified']
    assert not report['full_replacement_ready']
    assert 'private arbitrary' not in str(report)
    assert report['session_baseline_deltas']['internal_free'] == [0] * len(result['idle']['renewals_per_session'])


def test_intermediate_heap_floor_violation_cannot_hide_behind_valid_selected_snapshots():
    result, events, recovery = fixture()
    raw = serial(result)
    extra = copy.deepcopy(result['idle']['snapshots'][0])
    extra.update(uptime_ms=100001, internal_min=97000)
    raw = raw.replace('\nTLS heap', '\nMOQ_STATUS ' +
                      ' '.join(f'{key}={extra[key]}' for key in audit_module.STATUS_FIELDS) + '\nTLS heap', 1)
    report = audit_module.audit(result, raw, events, recovery)
    assert not report['operational_checks_pass']
    assert 'serial_resource_invariant' in report['findings']


@pytest.mark.parametrize('change,code', (
    (lambda r: r.update(pass_=False), 'runner_failed'),
    (lambda r: r.update(idle_monitor_survived=False), 'monitor_did_not_survive'),
    (lambda r: r.update(microphone_samples=1), 'unexpected_microphone'),
    (lambda r: r.update(forced_reconnect_ms=10001), 'final_reconnect_coverage'),
    (lambda r: r['idle'].update(renewals_per_session=[2, 0]), 'renewal_coverage'),
    (lambda r: r['idle'].update(elapsed_ms=119999), 'duration_incomplete'),
    (lambda r: r['idle']['reconnects'][0].update(duration_ms=10001), 'reconnect_too_slow'),
))
def test_runner_pass_flag_cannot_override_independent_acceptance_failures(change, code):
    result, events, recovery = fixture()
    change(result)
    report = audit_module.audit(result, serial(result), events, recovery)
    assert not report['operational_checks_pass'] and code in report['findings']


def test_a_short_smoke_cannot_claim_eight_hours_by_editing_result_duration():
    result, events, recovery = fixture()
    result['idle'].update(requested_seconds=28800, elapsed_ms=28800000)
    report = audit_module.audit(result, serial(result), events, recovery)
    assert not report['eight_hour_protocol_verified']
    assert {'event_duration_mismatch', 'reconnect_count', 'snapshot_duration_incomplete'} <= set(report['findings'])


def test_heap_delta_is_preserved_even_if_upstream_claims_recovery():
    result, events, recovery = fixture()
    result['idle']['cumulative_heap_recovery_verified'] = True
    for row in result['idle']['snapshots']:
        if row['elapsed_ms'] > 60000:
            row['internal_free'] -= 16
            row['native_rss_kib'] += 448
    report = audit_module.audit(result, serial(result), events, recovery)
    assert report['operational_checks_pass']
    assert report['session_baseline_deltas']['internal_free'] == [0, -16]
    assert report['session_baseline_deltas']['native_rss_kib'] == [0, 448]
    assert not report['cumulative_heap_recovery_verified']


@pytest.mark.parametrize('mutation,code', (
    (lambda raw: raw.replace('valid=1', 'valid=0', 1), 'tls_heap_invariant'),
    (lambda raw: raw.replace('allocations=5', 'allocations=6', 1), 'tls_heap_invariant'),
    (lambda raw: raw.replace('QUIC heap live=10', 'QUIC heap live=21', 1), 'quic_heap_invariant'),
    (lambda raw: raw.replace('uptime_ms=172500', 'uptime_ms=99999', 1), 'firmware_clock_regressed'),
    (lambda raw: raw + 'Guru Meditation Error\n', 'firmware_fault'),
))
def test_raw_serial_is_audited_independently_of_green_result_fields(mutation, code):
    result, events, recovery = fixture()
    report = audit_module.audit(result, mutation(serial(result)), events, recovery)
    assert not report['operational_checks_pass'] and code in report['findings']


def test_final_recovery_and_phase_events_are_required():
    result, events, recovery = fixture()
    report = audit_module.audit(result, serial(result), [], {})
    assert not report['operational_checks_pass']
    assert {'missing_phase_events', 'permanent_recovery_unverified'} <= set(report['findings'])


def test_missing_observation_windows_do_not_count_as_complete_resource_coverage():
    result, events, recovery = fixture(28800)
    raw = serial(result)
    result['idle']['snapshots'] = [row for row in result['idle']['snapshots']
                                 if not 100000 < row['elapsed_ms'] < 300000]
    report = audit_module.audit(result, raw, events, recovery)
    assert not report['operational_checks_pass'] and 'host_observation_gap' in report['findings']
    result, events, recovery = fixture()
    raw = '\n'.join(line for line in serial(result).splitlines()
                    if not line.startswith('MOQ_STATUS uptime_ms=1')) + '\n'
    report = audit_module.audit(result, raw, events, recovery)
    assert not report['operational_checks_pass'] and 'snapshot_not_in_serial' in report['findings']
