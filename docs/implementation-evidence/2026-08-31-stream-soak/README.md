# Operational duplex stream soak — 2026-08-31

The full Ultra shell now has an optional, explicitly USB-started synthetic MoQ
workload. Physical runs p117, p118 and p119 pass 500, 3,000 and 90,000 data groups
per direction. **p119 verifies the 30-minute operational stream-turnover gate**
on flash47, including exact receipts, renewal and permanent-host recovery.
The full WebRTC replacement goal remains open.

## Workload and isolation

`DOODAD_MOQ_STREAM_SOAK=ON` adds the workload to the ordinary full-shell MoQ
image. It does not use the old standalone diagnostic/bootstrap-bypass mode.
The native host uses a separate `stream-soak-fixture` build and explicit private
configuration. Ordinary builds exclude the synthetic path and reject that
configuration field. Persistent host services and the reference pin are unchanged.

Both sides exchange one valid three-byte, 20 ms mono Opus silence packet per
group, at 50 groups/s. Each direction also has one final receipt group, sent
only after receiving all peer data groups and completing the nominal duration.
Every group, payload and timestamp is checked; a fixed 64-bit rolling receive
window avoids retaining a run-sized history. Pacing permits bounded catch-up
with at least 10 ms between packets and fails on more than 200 ms of debt.
Missing, malformed, duplicate or abandoned groups fail this strict integrity
test. This does not impose zero PLC as an audio-quality requirement.

Normal enrollment, signed time, TLS bootstrap, WSS, scoped QUIC sessions,
catalog negotiation, control heartbeat and credential renewal remain active.
Neither microphone nor speaker is started, and no providers or ambient PCM
recordings are involved. Serial observation has a single USB owner. After
completion the runner verifies a fresh-grant reconnect; its outer hardware
driver reapplies permanent enrollment even when the test fails.

## Completed physical evidence

| Run | Data groups each way | Watch duration | Host duration | Renewals | Reconnect |
| --- | ---: | ---: | ---: | ---: | ---: |
| p117 | 500 | 10,033 ms | 10,000 ms | 1 | 6,297 ms |
| p118 | 3,000 | 60,049 ms | 60,002 ms | 3 | 6,246 ms |
| p119 | 90,000 | 1,800,064 ms | 1,800,002 ms | 82 | 6,274 ms |

All three runs use the same flash47 image and exact native binary recorded in
[source/binary hashes](source-snapshot.json). Each has matching send/receive
counts on both endpoints, zero send backpressure, zero final receive leases,
no firmware fault marker and passing TLS/ngtcp2 allocation gates. The renewed
session remains the same until the deliberate reconnect. Permanent readiness
was verified at revisions 173, 175 and 177 respectively. Full numeric telemetry is
in [hardware results](hardware-results.json).

During p118's three workload snapshots, free internal RAM is 164,603 bytes,
the lifetime internal minimum is 99,256 bytes and the largest free internal
block is 79,872 bytes. RX/TX high-water marks are 3/2. Host RSS measurements
rise from startup to about 19 MiB and are retained without claiming a leak-free
endurance result. Display timing samples cover baseline, workload and reconnect,
but no calibrated input-latency or full-UI stress gate is claimed. Native test
compilation overlapped early p118 startup/work; it is not a quiet-host latency
baseline. No other builds ran during p119.

p119 has 60 workload snapshots. Free internal RAM spans 164,535–164,635 bytes
and returns to **164,635 bytes**, its first observed workload value. The lifetime
minimum remains 99,256 bytes and the largest internal block 79,872 bytes. PSRAM
free space spans 7,008,812–7,016,356 bytes. Native-host RSS during the workload
spans 19,136–19,264 KiB. RX/TX high-water marks reach 6/3, with zero leased
frames in every snapshot. ngtcp2/TLS peaks are 30,344/49,419 bytes, with no
tracked denial or system allocation failure. Publisher cache drops, expiry,
failure and cancellation counters, control timeouts and transport pressure
counters are all zero at the final sample.

The final publisher sample contains 90,002 submitted and 90,001 retired groups,
including catalog traffic; it is not proof that all TX retirement callbacks
have completed at the instant of the final USB marker. Both peers independently
verify all 90,000 data groups and the additional receipt group. The subsequent
new connection is ready, but its internal free RAM is 32 bytes below the initial
idle baseline (164,603 versus 164,635); PSRAM free space is higher. This bounded
single-reconnect difference is retained for the cumulative endurance audit,
not declared a demonstrated leak or silently treated as zero.

[Flash evidence](firmware.json) records app0-only installation after live
identity, security and partition/OTA checks, followed by a successful full-shell
heartbeat. No bootloader, NVS, OTA-selection or user-data writes occurred. The
new firmware stays installed; factory restoration is not required.

## Verification and remaining scope

[Verification](verification.json) records 39 all-feature Rust tests, 31 default
Rust tests, 384 Python tests (four existing warnings), 46 native/firmware contract
checks, clean all-feature Clippy with warnings denied, and successful ordinary
and diagnostic full-shell firmware builds. The new tests actually decode the
fixture, exchange paced duplex groups through the native track model with final
receipts and continuing IPC, and reject malformed uplink input. Initial
non-Send actor compilation and Clippy style failures are preserved privately.
The final host and firmware binaries and source snapshots are also archived.

The 90,000-group operational turnover requirement is verified for flash47. These
synthetic tests do not cover codec CPU load, speech quality, speaker latency,
physical PTT, package interactions or sleep/wake. The failed p101/p112 speech
cells, 1,000-cycle and eight-hour tests, full UI/memory/latency, unchanged-reference,
ASan/CI and rollout gates remain open. No default switch, commit or push occurs.

## Work prepared while p119 ran

The working tree now also contains an idle/reconnect runner and a read-only
`VWMOQ1 STATS` owner snapshot, under the same optional firmware test flag. These
were added **after** flash47 was built and p119 started. They are not part of
flash47 firmware. Subsequent flash48 compilation and physical smoke results are
recorded in the separate [idle checkpoint](../2026-08-31-idle-soak/README.md).
The executed p119 runner/source is the privately archived p118 snapshot; later
working-tree edits did not alter that running Python process or the installed
image. Do not attribute future status-command or idle-test results to flash47.

The planned idle lane retains short credential renewals and fresh-grant
reconnects, requests status without opening audio, records host/device memory,
and has a two-minute smoke mode before the full eight-hour duration. Its
private certificates need ten hours of validity because the ordinary six-hour
bench certificate would expire before the intended test ended. Certificate time
checks and negative cases remain enabled. A successful protocol loop will still
require a separate cumulative-memory recovery audit before closing the full
endurance gate.

[Preparation verification](idle-preparation.json) records 23 passing checks for
the new idle harness at preparation time. The separate idle checkpoint records
later firmware and physical validation. Clock-model tests exercise the two-minute and
eight-hour reconnect schedules; none of these checks is an eight-hour hardware
pass.
