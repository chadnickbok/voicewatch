# Idle and reconnect endurance preparation

**Policy update, 2026-08-31:** the user stopped the long run and specified that
ten minutes is sufficient for now. Eight-hour and 1,000-cycle tests are optional,
not replacement gates. Historical acceptance flags below are superseded by this
decision, not retrospectively changed into passes. Further work is paused;
see the [optional longer-test outline](../../moq-optional-endurance-tests.md).

Flash48 runs the complete authenticated Ultra shell with the optional read-only
`VWMOQ1 STATS` command. The command queues an owner snapshot; it cannot start
microphone capture, publication, subscription or speaker playback. Ordinary
firmware excludes this diagnostic command. This checkpoint does not establish
eight-hour endurance or readiness to replace WebRTC by default.

## Physical smoke results

The first two-minute attempt, p120, fails in the host progress logger: the idle
helper passed `elapsed_ms` to a logger that already supplied that keyword.
The failure is retained. A regression reproduces the real logger contract;
renaming the helper's field to `idle_elapsed_ms` fixes it without changing
firmware, connection policy or acceptance thresholds.

The corrected p121 completes 120,039 ms of authenticated idle observation, one
planned fresh-grant reconnect in 6,265 ms, and a final reconnect in 6,262 ms.
Both idle sessions renew twice. Six snapshots pass the internal RAM floor,
largest-block and stack checks, with no active audio or retained media pools.
No microphone PCM is recorded and no provider or speaker is opened.

Free internal memory changes from 164,635 to 164,619 bytes after the planned
reconnect. That 16-byte difference is not yet attributed; it is neither evidence
of zero cumulative loss nor sufficient on its own to diagnose a leak. Native
RSS spans 18,400–18,848 KiB and Python RSS 62,096–62,544 KiB. Host measurements
include the test and observation machinery. The separate cumulative resource
audit remains required.

Permanent enrollment is reapplied at revision 181, with a fresh permanent-host
readiness event and the existing supervisor, child and legacy service alive.
No factory firmware restoration is performed or required. Flash48 remains
installed; temporary credential cleanup does not write firmware.

## Eight-hour run

p122 started on the unchanged, archived flash48 firmware and normal native
host. Authenticated idle readiness, renewal and status sampling were observed.
It was interrupted at the user's request. The incomplete result is retained;
it is neither an eight-hour pass nor a product-failure diagnosis.

[Interruption and recovery receipt](interrupted-run.json) records the observed
duration and counters. Permanent enrollment is reapplied at revision 183 and a
fresh normal-host readiness event is verified. The temporary test processes and
serial monitor have stopped; flash48 remains installed.

The harness retains 45-second credential leases and schedules seven hourly
fresh-grant reconnects, then a final reconnect. Private test certificates last
ten hours so certificate expiry does not truncate the eight-hour workload;
certificate verification remains enabled. A process-scoped macOS idle-sleep
assertion lasts only for this run. The serial monitor only requests numeric
status and keeps raw logs private. The outer runner reapplies permanent
enrollment on completion or failure.

Completing the protocol loop alone does not close the cumulative-memory gate.
Review device and host trends, all pool/stack observations, unexpected restarts,
audio inactivity, authorization renewal and recovery before accepting the run.
The 1,000 PTT/echo-cycle test is also optional follow-up. Functional requirements
in the rest of the product matrix remain separate.

## Verification and provenance

[Verification](verification.json) records 407 passing Python tests (four
warnings), including 23 idle-harness tests, 46 firmware/native contract checks,
and successful normal and diagnostic firmware builds. The pre-fix two-test
logger failure is retained. Clock-model tests cover both reconnect schedules;
they are not physical endurance evidence.

[Hardware results](hardware-results.json) contain completed runs only.
[Firmware receipt](firmware.json) and [source/binary hashes](source-snapshot.json)
identify flash48 and the tested sources. Exact binaries, source copies, raw
logs, device identities, keys and profiles are archived privately. The earlier
[30-minute duplex soak](../2026-08-31-stream-soak/README.md) ran on flash47;
do not attribute that test to the new firmware without appropriate validation.

No default switch, persistent-service update, reference-pin change, commit or
push is part of this checkpoint. Speech impairment failures, full physical
interaction/package/sleep-wake checks, calibrated latency, complete memory and
allocation validation, unchanged-reference interoperability, sanitizer/CI and
release checks remain open.

## Independent completed-run resource audit

`tools/moq_idle_audit.py` reads the completed result, private serial log, phase
events and permanent-recovery receipt without opening USB or changing runtime
state. It checks elapsed-time evidence, reconnect/renewal coverage, all parsed
status and TLS/QUIC allocation records, snapshot membership, monotonic clocks
and final recovery. Status gaps over 20 seconds and host-snapshot gaps over
60 seconds fail observation coverage; these bounds allow the planned reconnect
and settling time around the five-second/30-second sampling cadences.

[Audit results](resource-audit.json) preserve p120 as failed and independently
pass p121's operational and sampled-resource checks. All 31 complete status
records and 34 snapshots for each tracked allocator are checked. Per-session
envelopes retain the 16-byte internal-heap reduction and host RSS changes rather
than reducing the result to a final boolean. RSS, sampled free heap and live
allocator snapshots cannot alone establish whole-system leak freedom; the
cumulative recovery and eight-hour acceptance flags remain false.

The audit was added while p122 runs and does not change its archived firmware,
host or harness. [Audit verification](resource-audit-verification.json) records
42 focused tests and 426 full Python tests (four warnings). Those tests include
missing observations, incomplete duration, faults, inconsistent allocation
accounting and unverified permanent recovery. They do not replace hardware
evidence. The earlier 407-test result remains the pre-run harness checkpoint.

A fresh Linux sanitizer environment probe remained in Docker's `created`
state with no workload PID. The owned probe was explicitly cancelled and
removed; no sanitizer test ran. No unrelated container or Docker service was
restarted, and ASan remains an open gate.
