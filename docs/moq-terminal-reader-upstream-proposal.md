# Draft upstream proposal: retain Lite05 readers through terminal completion

Prepared for review; **not submitted** to moq-dev.

Suggested title: `fix(moq-net): drain promised Lite05 groups before terminal completion`

## Problem

On base revision `eb5776e21eeaecba8e844be53c821895c178bcaf`, a subscriber can
publish END to its track model and remove routing on control FIN while an
independently scheduled UNI group reader has not run yet. QUIC may already have
acknowledged all sender bytes, but the consumer loses a promised tail group.

The reproducer delays one accepted UNI reader by 200 ms on loopback. It does not
delay QUIC acknowledgements, change the wire encoding or inject packet loss.
The unmodified receiver reports seven of eight frames and misses terminal
receipts. The same exchange passes with the proposed patch.

## Proposed behavior

Retain subscription routing and known readers until the promised range has been
resolved and active readers have completed or aborted. Keep START, demand floor
and END distinct. Merge resolved ranges into a frontier and at most 64 disjoint
intervals; permit at most 128 active readers per subscription. Capacity overflow
fails the subscription rather than allocating unbounded state.

Control FIN starts a single drain deadline, clamped to 1 ms–5 s using the
requested latency. Later updates cannot extend it. Missing sequences at expiry
fail explicitly; they are not fabricated as delivered groups. Capped END without
FIN can remain parked. DROP, known reset, cancellation and subscription
replacement retain their ownership semantics. Model completion preserves the
actual terminal boundary even when an explicit drop resolves the tail.

No wire codec, ALPN or authentication changes are proposed. The implementation
is enabled for Lite05; older wire versions retain their existing behavior.

## Reviewable patch and tests

Patch: `libs/moq-esp32/tests/interop/reference-candidate/terminal-drain.patch`.
SHA-256: `14d48121c8039da548d82086122d0e80e3e2cc0734a46b87cee2dce45f521239`.

The recipe validates the clean base revision and patched file hashes before
building a separate peer. Instructions and nine real-QUIC lifecycle cases are
in that directory's README. Tests cover late FIN, active DROP, known/unknown
reset, changing demand, parked END, fixed deadlines and cancel/replacement, with
an unrelated track required to continue in each case.

Current local validation passes 853 moq-net unit tests, 61 kio unit tests, 39
VoiceWatch host tests and 176 real-QUIC integration cases with a UBSan client.
The unchanged-reference failing reproduction remains available independently.

## Limits for upstream review

This patch is larger than a single completion check because range accounting,
reader lifetime, demand changes and model completion interact. Review the
bounded tracking and cancellation paths together. Missing unreliable datagrams
and wholly unread reliable groups share a conservative timeout failure policy;
datagram-specific loss semantics and broader subscriber/source churn need
separate consideration. VoiceWatch's initial use is reliable-stream audio.

VoiceWatch has authorized maintaining this exact patch while seeking upstream
review. That is a downstream adoption decision, not upstream endorsement or a
claim that every MoQ use case is validated.
