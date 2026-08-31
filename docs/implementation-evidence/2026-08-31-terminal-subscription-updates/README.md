# Subscription-update and real-QUIC lifecycle candidate

The opt-in Rust receiver candidate now passes the two subscription-update
regressions discovered after the prior terminal-drain checkpoint. This is progress
toward the complete WebRTC replacement plan, not library or product acceptance.
The compatibility pin, production C runtime, host deployment and Ultra firmware
are unchanged.

## Corrections

Quiet subscription aggregation previously returned a contributing value without
registering its value-change waiter. A cap update reached the inner subscriber
but never woke the upstream aggregate. The candidate now computes the contribution
and arms the waiter under the same lock, excluding closed subscribers there too.
The explicit departure waiter remains. The new unit regression failed before
the correction and passes afterward.

The receive-drain floor previously stayed at the initial request. Advancing
SUBSCRIBE_UPDATE could therefore leave withdrawn groups as unresolved holes.
After a successful wire update the candidate now advances that floor, compacts
resolved ranges, wakes and cancels lower active readers, and rejects late lower
groups. Announced START is stored separately: moving the requested floor beyond
END does not alter or invalidate the original boundary. Once control FIN starts
the drain deadline, later latency changes cannot prolong it.

The former WIP patch has been incorporated into the maintained candidate patch;
the previous WIP and failing regression remain available in library commit
`64c98db`. Historical evidence under `2026-08-31-terminal-drain-candidate` is
unchanged and refers to its original manifest. See [regressions-before.json](regressions-before.json)
for the observed pre-fix failures and log hashes.

## Results from fresh preparation

| Check | Result |
| --- | --- |
| Complete reference library unit suite | 853 passed |
| Ordinary native interoperability matrix | 18 passed |
| Concurrent engine exchanges, at most four peers and two Tokio workers each | 100/100 passed |
| Delayed UNI readers 3–11, 200 ms, three repetitions | 27/27 passed |
| Nine real-QUIC lifecycle fixtures, three repetitions | 27/27 passed |
| Original pinned peer, reader 4 delayed 200 ms | Still fails; group 7 missing |

The nine lifecycle cases cover late group FIN; DROP of an active partial frame
and absent tail; known-group RESET; RESET before an identifiable header; requested
floor advancement; cancellation of an active reader below the new floor; raising
a parked cap; immutable FIN deadlines after a latency update; and unsubscribe
followed by replacement with a fresh subscription ID. Every fixture must complete
an unrelated track on the same connection. Frame payloads, timestamps and exclusive
END are checked on successful paths; incomplete frames must fail on aborted paths.

These fixtures use a bounded scripted publisher and the public Rust subscriber
API over real loopback QUIC, with TLS 1.3, trusted ephemeral certificates, hostname
verification and the selected ALPN. They exercise the reference receiver; they
are separate from the C adapter cases in the ordinary matrix. No certificate
validation or payload assertion is bypassed.

The two timeout fixtures reject automatic retries of the faulted audio track to
observe unrelated-track recovery independently. The latency-update fixture needed
a fixture correction: retry requests may legitimately carry the updated latency,
so they must be parsed and rejected before checking the initial-request latency.
That fixture-only failure is not counted as a receiver defect. The cap parks for
600 ms in the real-QUIC fixture, and virtual-time unit coverage parks for 60 seconds.
The fixed FIN deadline is tested both with real QUIC and exact virtual-time bounds.

The baseline still reports all 11 sender groups retired, no sender expiry/failure/
cancellation/cache drop, and a delayed reader discarded before reading. It fails
the original payload/terminal assertions; no expectation is weakened to accept it.
See [baseline.json](baseline.json), [verification.json](verification.json),
[native-results.json](native-results.json), and [lifecycle-results.json](lifecycle-results.json).

## Reproduction and source binding

Use the library's [candidate instructions](../../../libs/moq-esp32/tests/interop/reference-candidate/README.md),
including `--fault-peer` when invoking the validator. Preparation creates a fresh
local clone of the unchanged base, checks the patch and every patched-file hash,
and copies versioned synthetic test sources. The first offline peer build adjusts
only its isolated source redirects; the subsequent locked build succeeds. The
full lock comparison is identical after normalizing local `moq-net` and matching
unchanged `kio` references. Normal dependencies, locks and baseline binaries stay
unchanged. The existing delayed-session wrapper's unused-result warning remains;
it does not prevent the peer build.

All three test binaries retain their hashes throughout validation. Source hashes,
base revisions, the isolated lock hash and log hashes are in
[source-snapshot.json](source-snapshot.json). The validator owns helper process
groups so a timed-out subprocess cannot leave its spawned test peers running.
An injected timeout test confirms that both the owned helper and its child are
no longer live afterward.
Binaries, credentials, private service state and audio recordings are not included.

## Remaining full-plan requirements

This patch has not been submitted upstream or adopted by the compatibility pin.
The unchanged-reference acceptance gate remains open. Broader loss/reordering,
source replacement, backward seeks, subscriber combinations and repeated fault/
reconnect churn need coverage. Datagram loss handling remains conservative.

The full-plan gates for TLS/native hard allocation limits, impaired speech quality,
the complete impairment matrix, long responses and proactive renewal, physical
PTT/navigation/app/package/sleep-wake behavior, latency, 1,000 interaction cycles,
eight-hour soak, security negatives, CI and distribution review remain open.
Passing these synthetic receiver cases does not satisfy those requirements.

No device access, capture, flash, enrollment change or firmware restoration was
performed. Both persistent Mac services retain their prior processes (MoQ 45731,
legacy 2759). The installed flash35 evidence remains historical hardware evidence,
not a new acceptance run.
