# Isolated terminal receive correction

This checkpoint implements and validates an opt-in reference receiver correction.
It does not close the unchanged-reference acceptance gate or the full WebRTC
replacement goal. The production library runtime, compatibility pin, installed
host, firmware and enrollment are unchanged.

## Result

The original pinned peer still fails the 200 ms delayed-reader reproduction:
audio group 7 is absent, the delayed reader is dropped before reading, and the C
publisher reports all 11 groups retired with zero expiry/failure/cancellation/drop.
The candidate preserves all nine audio groups and the exact payload/timestamp,
catalog and terminal ACK checks.

After preparing a fresh source copy using the maintained patch:

| Validation | Result |
| --- | --- |
| Complete reference library unit suite | 849 passed |
| Ordinary native interoperability matrix | 18 cases passed |
| Engine stress, four simultaneous peers, two Tokio workers per peer | 100/100 passed |
| 200 ms delayed-reader sweep, UNI indices 3–11, three repetitions | 27/27 passed |
| Original unchanged peer, delayed reader index 4 | Failed, as before |

Expected TLS/ALPN rejection cases are successful matrix cases even though their
client connection reports `pass=false`. The normal matrix still checks 10,000 raw
streams per direction, 65 bidi replies, audio, engine/service/endpoint operation,
catalog fallback, and security rejection. No test acceptance count was reduced.

The first draft already passed the native reproduction, but further regression
tests found two issues in that draft: an ordered reader could wait forever after
an explicitly dropped tail, and a cancelled datagram could mutate the model
before discovering cancellation. Both regressions were observed failing and then
fixed before the final validation. The END/FIN deadline policy also explicitly
preserves a valid subscription parked at its serving cap.

## Implementation and provenance

The library now carries `tests/interop/reference-candidate/terminal-drain.patch`,
its base/file hashes and license, plus preparation and validation tools. Preparation
uses a fresh local clone of the exact original reference, applies the checked
patch, and copies only versioned synthetic peer/oracle inputs. Normal dependency
sources, lockfiles and binaries are left intact. The candidate peer lock redirects
only `moq-net` and the unchanged matching `kio` to local sources; dependency
versions and other Git revisions are unchanged.

The patch separates announced END from completed receive work. It retains the
subscription through FIN while unresolved sequences or active group readers remain.
START, completed groups and DROP update a contiguous frontier with at most 64
disjoint intervals; active reader storage is capped at 128 per subscription.
DROP aborts matching readers; cancellation removes registration, including during
an interrupted SUBSCRIBE opening, and cannot resolve a replacement generation.
No wire encoding or sender scheduling changes are introduced.

Control FIN starts a fixed drain deadline using the requested maximum latency,
clamped to 1 ms–5 s. END alone does not start that timer because a serving cap may
legally park the subscription. Missing sequences time out as a scoped failure.
Unknown losses cannot be classified as reliable groups versus datagrams, so the
candidate does not silently turn either into successful completion. Model readers
finish with the original END even when trailing sequences were explicitly dropped.

Unit coverage includes 100,000 groups with compacted bookkeeping, range/reader
exhaustion, missing groups, bounded drain timing, capped subscriptions, DROP,
cancellation and replacement isolation, establishment cleanup, malformed bounds,
and both model cursor types. These fixed metadata bounds do not establish a hard
allocation cap for Rust, TLS, transport buffers, or the complete process.

See [verification.json](verification.json), [native-results.json](native-results.json),
[baseline.json](baseline.json), and [source-snapshot.json](source-snapshot.json).
The unit-tested source and prepared source both match the recorded patch manifest.
The final validation verifies unchanged client and candidate binary hashes through
all native cases. Logs are hashed; binaries and private deployment data are not
included. The existing delayed-reader wrapper retains its pre-existing unused
Result compiler warning; the build succeeds.

## Remaining work

This is a candidate patch, not upstream acceptance. Real-transport coverage for
DROP/reset races, reset before header ingestion, loss/reordering, source changes,
and changing subscription bounds must be expanded before adoption. The conservative
datagram behavior needs review. No upstream PR, compatibility-pin update or host
rollout has occurred; the plan's unchanged-reference gate remains open.

Both persistent Mac services retain their prior PIDs (MoQ 45731, legacy 2759).
No device access, capture, flash, enrollment change or firmware restoration occurs
in this checkpoint. Previous flash35 evidence remains historical, not a new
hardware acceptance run. TLS/native allocation, impaired speech, physical shell
and app parity, latency, long responses and release-soak gates remain open.
