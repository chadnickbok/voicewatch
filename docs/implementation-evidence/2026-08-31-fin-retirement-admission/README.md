# Submitted-FIN admission and physical speech checks

This checkpoint corrects an adapter admission bottleneck and runs new physical
speech tests on the full Ultra shell. The correction does not yet satisfy the
impaired-speech gate or complete the WebRTC replacement plan.

## Correction and ownership proof

The adapter counted a local send half as active until it was reset or closed,
even after ngtcp2 accepted all its bytes and FIN. With delayed acknowledgements,
finished submissions could therefore exhaust the twelve local active slots
while the bounded retirement reserve still had room. The scheduler already had
no new bytes or FIN to submit for those streams.

A submitted FIN now permits that send half to use the existing retirement
reserve, like a stopped send half. A merely requested or blocked FIN remains
active. No stream ID or payload buffer is released early: ACK-owned bytes stay
available for retransmission, and a metadata slot remains owned until definitive
backend closure. The 16 active slots, 32 total slots, four default peer-bidi
reservations, 64 payload blocks, 128 KiB QUIC heap cap and 200 ms media/loss
policies are unchanged.

The new test compiles the actual adapter with mocked ngtcp2 callbacks and real
loopback UDP sockets. It first fills the active slots with unsent FIN requests
and requires rejection without consuming an ID. After pumping the bytes and
FINs, it withholds all ACKs/closes and requires fresh admission through the
bounded reserve. It fills all 28 local metadata slots, confirms rejection at
the hard bound, and opens all four reserved peer reply halves. An ACK releases
only its data; definitive closure is still required before reusing its metadata
capacity. Other streams retain their exact bytes and IDs throughout.

This regression fails on the prior admission logic and passes after the change.
The normal adapter suite, including 200,000 stream lifetimes, and portable host
suites pass. All 18 native interop/security cases pass against the unchanged
pinned reference. This ordinary matrix does not close the separate delayed-tail
defect documented in the receiver-candidate checkpoints.

## Firmware and physical results

Flash36 builds with ESP-IDF 5.5.5, MoQ uplink, quad PSRAM and certificate date
validation enabled. Its image is 3,428,432 bytes; SHA-256 is
`ef656b468a63094684302d5e88b3a03d3f5e2f5a3f67b3b677213728410d31a2`.
The app-only runner verifies the connected chip, security state, flash size,
partition table and OTA selection, writes only app0, and observes the complete
shell's heartbeat. No NVS, bootloader, OTA metadata, package or user-data erase
is performed. The updated firmware remains installed; no firmware restoration
is required.

| Run | Configuration | Result |
| --- | --- | --- |
| p75 | Installed flash35; 5% loss, 120 ms added RTT, 800 ms uplink blackout | FAIL. Recovery 674 ms; no accepted full provider turn. Capture conceals 8,640/58,720 samples (27 groups); two word errors exceed the fixed gate. Local slot-denial count reaches 77. |
| p76 | Flash36; same impairment settings | FAIL overall. Recovery 838 ms; two full provider turns pass with zero word errors. Third turn conceals 10,240/58,400 samples (32 groups), has three word errors and loses the critical phrase. Local slot denials reach 16. |
| p77 | Flash36; no induced impairment | PASS. Three full provider turns have zero word errors, fresh watch reads, exact speaker receipts and idle reconnect. Capture has no lost/late groups or concealment. |

p76's first two completed captures conceal 1,920/58,560 and 7,360/58,400 samples.
The local-denial counter stays at two through those turns, then rises during the
third. The peak sampled ngtcp2 heap is 45,456 bytes, below 131,072, with no
allocation denial or platform failure. This is ngtcp2-owned storage, not a bound
on TLS, the entire firmware, or the native host process.

The same seed (52) does not make these physical packet timelines identical. The
unit regression establishes the admission defect and correction. The physical
runs establish current outcomes, but cannot isolate the correction's effect on
speech accuracy. Two successful impaired turns do not pass a three-turn gate.
The six-word error/critical-phrase policy was not relaxed after failure.

Only fixed test scores and numeric diagnostics are published. Microphone PCM is
counted/discarded rather than recorded; the synthetic phrase is unchanged.
Mac output volume/mute is restored by each bench. Afterward, permanent service
enrollment advances to revision 114 with the same device identity, trust roots
and service key. This reconnects the service without restoring firmware. Both
persistent Mac services remain running; no receiver-candidate patch or native
host deployment is installed by this checkpoint.

See [hardware-results.json](hardware-results.json), [firmware.json](firmware.json),
[native-matrix.json](native-matrix.json), [verification.json](verification.json)
and [source-snapshot.json](source-snapshot.json).

## Sanitizer limitation and remaining work

Fresh sanitizer verification is **not a pass**. The Linux Docker container
remained in `Created`; an independent `/bin/true` container also could not start.
Both created containers were removed and their waiting CLI processes stopped.
On macOS, the instrumented adapter process stalled before `main`; a stack sample
places it in ASan shadow-memory initialization (`MemoryRangeIsAvailable` /
`get_dyld_hdr`). It was stopped without producing a test result. Log/sample hashes
are recorded, and sanitizer verification must be rerun in a functioning runtime.
These startup failures are not evidence of an adapter memory-safety failure.

Remaining pressure and abandonment under impairment still need diagnosis. The
complete loss/RTT/reorder/burst/flow-control matrix, impaired speech, long responses,
physical PTT/navigation/installed-app/package/sleep-wake behavior, TLS/native
allocation caps, security negatives, latency, interaction/idle soaks, remote CI
and distribution review remain required. The unmodified-reference delayed-tail
gate is also open. WebRTC remains the configured default.
