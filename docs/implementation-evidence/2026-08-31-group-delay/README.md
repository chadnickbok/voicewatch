# Delayed Hang group on the full Ultra shell — 2026-08-31

The private diagnostic host deliberately delayed one standard Hang audio group
while later groups continued. On unchanged flash46, p116 passed three real
microphone/STT/tool/TTS turns and idle reconnect. This adds a physical delayed
application-group case to the original acceptance matrix. It does not complete
the full replacement goal or the remaining impairment/product gates.

## Fault and physical observations

The `group-delay-fixture` build holds the eighth encoded 20 ms group once per
session, with at most one 1,275-byte Opus payload retained. It uses the pinned
public group/frame APIs, without a protocol patch, fake wire format or sleeping
media pump. A later PCM step releases the group after 250 ms; end, cancel and
owner destruction explicitly abort an unreleased group. No detached sender can
publish into a replacement. Default binaries reject the diagnostic config field
and contain neither its interception path nor held-group state.

[Hardware results](hardware-results.json) record:

- A 59-byte packet held for **257,756 µs**, with 12 newer groups published before
  release. The first newer group was published 21,112 µs into the hold.
- The watch observed a fresh 200,000 µs media timestamp before the delayed
  140,000 µs timestamp. Its maximum inter-frame arrival gap in the affected
  response was 58,021 µs, shorter than the deliberate hold. Thus later received
  audio progressed while the older group was missing.
- One concealed chunk and one late chunk in the affected response, zero queue
  pressure and zero fallback silence. Both later responses had zero recorded
  concealment/late counts. This is expected counted degradation, not a failed
  zero-PLC product gate or a subjective audibility measurement.
- Three zero-word-error microphone turns; response totals 42,360, 41,149 and
  41,149 samples. All three watch completion receipts, detailed serial summaries
  and final serial markers match **124,658 samples**. Counts include concealment;
  they do not establish bit-identical decoded PCM under loss.
- Clean service startup/shutdown, idle reconnect, no firmware fault marker,
  TLS peak 49,420 bytes and ngtcp2 peak 30,344 bytes, without tracked allocation
  denial/system failure. This is not full-shell memory stress/headroom proof.

Permanent enrollment was reapplied at revision 171 and fresh readiness verified.
The persistent MoQ and legacy service processes were not restarted or updated.
No firmware was written/restored, no default or reference pin changed, and no
commit/push was made.

## Native verification and provenance

The final suites pass 35 Rust tests with the feature and 30 without it, 384
Python tests and 46 real-native/firmware contract checks. Five new delay tests
cover fresh group/control progress, late release, cancel/replacement, short
response and fixed diagnostic bounds. A separate config test verifies that the
field is rejected without the explicit build feature. Clippy passes for both
builds with warnings denied. See [verification](verification.json).

The initial fixture tests exposed an error in the new diagnostic code: dropping
an unfinished producer did not retire its cached group. Explicit abort fixed it.
A short-response test was also corrected to acquire its reader before abortion;
aborted groups need not remain discoverable to future cache readers. These
failed development runs are archived; neither is relabelled as a production
transport defect. The initial Clippy boolean warning is also retained.

The final artifact check also caught feature tests replacing the ordinary
`target/debug` executable. The default binary was rebuilt, checked for absence
of diagnostic configuration/interception, and the native contract lane rerun.
Feature builds/tests should use their separate target directory. The physical
run used its own diagnostic path and the permanent host was unaffected.

[Source and binary hashes](source-snapshot.json) distinguish the exact diagnostic
executable used by p116 from the later rebuild. A config test, equivalent boolean
simplification/formatting, and a missing/truncated-log failure guard were added
after the physical run. The rebuilt diagnostic binary is **not** claimed
bit-identical or independently hardware-tested. The exact executed binary and
raw logs are preserved privately. The public artifacts contain numeric metrics,
fixed labels and hashes, never credentials, device identifiers, transcripts or
ambient microphone recordings. Pre-cleanup manifests are also retained.

The production firmware remains the image recorded in the
[previous flash46 checkpoint](../2026-08-31-blocked-stream/firmware.json).

## Remaining scope

The native actor test proves control replies during a held group; the physical
run proves later audio arrival, complete responses and reconnect. It does not
measure physical button/touch latency, demonstrate on-device QUIC byte-credit
blocking, or validate every delay/loss combination. Earlier p101/p112 speech
failures and p115's incomplete serial statistics remain unchanged. Full UI/app
package/sleep-wake interaction, calibrated latency, complete SRAM stress,
90,000-group and 1,000-cycle/eight-hour endurance, unchanged-reference and ASan/CI
release gates remain open. No new production receive/player defect was exposed
by this delayed-group case.
