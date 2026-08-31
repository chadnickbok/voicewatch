# Control/media admission and capture idle recovery

This checkpoint fixes two demonstrated defects without widening media deadlines,
loss budgets, queues, stream pools or the QUIC heap cap. It does not complete the
WebRTC replacement plan. Flash37 remains installed; the modified native host is
tested only in temporary benches, not deployed to the permanent service.

## Independent media admission

The engine previously returned after queueing DROP reports, then skipped a
subscription entirely while its control token was pending. Even a writable DROP
therefore cost another owner poll before fresh media admission. A blocked control
stream could defer otherwise eligible media indefinitely until the control timeout.

A strengthened existing regression fails on the former code at the first fresh
group assertion. The correction admits contiguous media after START has been fully
submitted, including in the same poll as a DROP batch and while its write is blocked
or partial. The existing control token, owned bytes and partial offset remain intact.
No second control write can overwrite them. A new cache gap waits until its own DROP
can be queued, while serving caps, peer cancellation and terminal ordering still apply.

Coverage includes twelve simultaneous expiries, continuing input while DROP is
blocked, blocked and byte-fragmented START, partial DROP, cap updates, multiple cache
gaps, exact nonduplicated DROP reports, END after media retirement, and peer FIN.
The full host and actual-adapter suites pass. The macOS engine arenas remain
23,496 bytes core, 204,608 media and 68,064 TX. Adapter storage remains 30,776 bytes
with 16 active slots, 32 metadata slots and 64 payload blocks. No hot-path allocation
or public API change is introduced.

All 18 ordinary native interoperability/security cases pass against the unchanged
reference. This does not close its separate delayed-tail failure. An additional
engine run with UBSan and nonrecovering checks passes; its unchanged zlib objects
are not instrumented. A complete fresh ASan/UBSan run remains outstanding after the
previous runtime startup failures; this partial check is not presented as that gate.

## Capture idle failure scope

p78 exposed another problem: the host closed the authenticated MoQ session when
no usable group or authenticated end arrived within its three-second capture idle
deadline. That absence was a generic fatal error, unlike excessive media loss,
which already emitted an identity-bound `capture.failed` and retired only the turn.

The idle branch now uses that same typed capture-loss outcome. Its three-second
deadline is unchanged. Malformed frames, invalid authorization, IPC pressure and
transport failure remain fatal. The actor regression now includes no initial audio
and a stall after legitimate PCM, alongside excessive loss and malformed media.
It fails before the correction and passes afterward. It verifies the failed identity,
continued control, rejection of stale end/playback callbacks, and a fresh capture
with exact PCM on the same origins. All 28 native host tests pass.

This is a narrow correction of the observed idle branch, not a catch-all conversion
of protocol errors to recoverable outcomes. Broader capture lifetime and transport
fault coverage remains required.

## Physical results

Flash37 is a full Ultra shell build with MoQ uplink, quad PSRAM and certificate
date validation. Its 3,428,400-byte image has SHA-256
`e10d61e4fb15a416a8dadcdd23b420e637a5def368b4e03f40048fb21555cf74`.
Device identity, security state, flash geometry, partitions and OTA selection were
checked before the app0-only write. The write verifies and the shell heartbeat passes.
No NVS, bootloader, OTA metadata, package or user-data erase is performed.

| Run | Host / network | Result |
| --- | --- | --- |
| p78 | Previous host; 5% loss, 120 ms added RTT, seed 52, 800 ms uplink outage | FAIL before speech. The capture idle deadline retires the session; no complete turn. |
| p79 | Previous host; no induced impairment | PASS. Three complete turns, zero word errors, zero lost/late capture groups or concealment, exact speaker receipts and reconnect. |
| p80 | Idle fix; same impairment settings as p78 | FAIL overall. Outage capture aborts without STT commit and preserves the session; recovery is 908 ms. Two complete turns have zero word errors. Third capture exceeds the loss budget and aborts before transcription. |
| p81 | Idle fix; no induced impairment | PASS. Three complete turns, zero word errors, no concealed audio samples, exact speaker receipts and reconnect. One initial reset group is missing and recovered without losing audio samples. |

p80's completed captures report 3 and 5 lost groups, with 744/58,880 and
1,600/58,720 samples concealed. Its third capture encounters an eleven-group gap
(523 through 533), exceeding the unchanged 200 ms loss policy. The final sampled
network-owner maximum poll gap is 203 ms, with thirteen stale capture-service
frames and an eight-frame queue high-water mark. Local slot denials remain zero;
the sampled ngtcp2 heap peaks at 48,064/131,072 bytes with no allocation denial or
platform failure. These observations motivate investigating owner-task scheduling
and cache expiry; they do not establish the cause of the physical gap.

The same seed does not produce identical physical packet timelines. p80 recovers
through an excessive-gap path rather than repeating p78's three-second idle path;
the latter correction is directly verified by the actor regression. Neither two
successful impaired turns nor clean runs satisfy the full impairment gate. No
speech threshold, codec setting, reorder wait or concealment budget was relaxed.

The clean p81 result includes one missing reset group, so it is not a zero-group-loss
claim. All three playbacks have zero concealed, late, pressure or silence counts.
Only synthetic-fixture scores and numeric diagnostics are published; ambient PCM,
credentials, raw provider/serial logs and binaries remain private. The benches that
changed Mac volume restored it; p78 failed before changing it.

Permanent enrollment is reapplied at revision 119 with the same identity, trust roots
and service key. A new permanent-service `moq.session_ready` event is observed. Both
persistent Mac services retain their existing processes. No firmware restoration,
production native-host update, compatibility-pin change or candidate rollout occurs.

See [hardware-results.json](hardware-results.json), [verification.json](verification.json),
[firmware.json](firmware.json), [native-matrix.json](native-matrix.json), and
[source-snapshot.json](source-snapshot.json). Older checkpoints and hashes are unchanged.

## Remaining full-plan work

The next concrete investigation is the 203 ms network-owner gap and stale capture
frames observed before p80's failed third turn. The full loss/RTT/burst/reorder/
duplication/flow-control matrix, impaired speech, 600-second responses, proactive
renewal, physical PTT/navigation/installed-app/package/sleep-wake checks, latency,
TLS/native allocation bounds, security negatives, unchanged-reference acceptance,
1,000 interactions, eight-hour soak, remote CI and distribution review remain open.
WebRTC remains the configured default. These two fixes do not close all nine findings.
