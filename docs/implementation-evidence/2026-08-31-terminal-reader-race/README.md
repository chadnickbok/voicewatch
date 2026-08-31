# Terminal loss localized to reference reader scheduling

This checkpoint adds a reproducible failing test, not a completed terminal
interoperability fix. The full replacement goal remains open. No firmware,
watch enrollment, deployed host or default transport is changed.

## Evidence

With the sender's END-retirement guard from the preceding checkpoint, 100 serial
engine exchanges pass. Four concurrent isolated loopback cases (two Tokio workers
per peer), repeated for 40 connections, then reproduce one failure: the reference
delivers only groups 3–8. The sender reports all 11 groups submitted and retired,
with zero expiry, cancellation, failure and cache eviction. These include two
catalog groups and nine audio groups. Transport retirement is therefore not proof
that the reference application ingested all groups.

The new `tests/interop/reproduce_terminal.py` selects one accepted UNI stream and
delays its first read by 200 ms. QUIC continues receiving and acknowledging; all
wire bytes, priority rules and exact sequence/payload assertions remain unchanged.
The delay is below the requested two-second media latency. A sweep over accepted
UNI indices 3–11 fails eight of nine cases. The final index-4 reproduction loses
audio group 7: eight groups/seven frames arrive, the sender retires all 11, and
the delayed reader is dropped before its first read. The receiver's final empty
group does not establish that the prior groups were ingested.

The production adapter/core is unchanged this checkpoint. Normal peer operation
still passes the raw QUIC session directly to the pinned `moq-net::Server`.
Only the explicit diagnostic uses a scheduling wrapper, with cancellation-safe
delay state and a maximum one-second delay. The wrapper calls the original stream
methods, preserves bytes, and delegates closure, datagrams and stats. It is not
used by the deployed host or the Ultra. No pinned dependency source is edited.

## Relevant pinned implementation

Reference: `eb5776e21eeaecba8e844be53c821895c178bcaf`.

- `rs/moq-net/src/lite/subscriber.rs`, `run_uni` / `recv_group`: accepted stream
  readers run as separate futures. QUIC can ACK before a reader decodes the
  subscription ID and inserts its group into the model.
- In the same file, `Event::SubResponse(End)` calls `serving.finish_at`.
  `Event::SubClosed(Ok(()))` tears down the subscription and removes its ID from
  `subscribes`, so a later group reader can no longer resolve that ID.
- `rs/moq-net/src/model/track.rs`, `TrackState::is_complete`: a final boundary
  plus the highest sequence is enough to declare completion; it does not account
  for unresolved earlier groups.
- `rs/moq-net/src/model/resume.rs`, `poll_segment`: a completed underlying
  subscriber becomes `Done`, so late insertion does not reopen that cursor.

These mechanisms explain why the C sender's ACK-retirement barrier alone cannot
guarantee application ingestion. An arbitrary sender sleep would mask scheduling
instead of establish a protocol invariant and is not added.

## Required receiver-side correction

The next fix must separate an announced terminal boundary from completed receive
work. It must retain subscription ownership while outstanding groups below END
are unresolved; START, DROP, reset and receive deadlines must resolve that range
without waiting forever for unavailable groups. Completion tracking must be
bounded, tolerate groups before START and out of order, and preserve unrelated
subscriptions. Mixed datagrams, cancellation, source replacement and malformed
bounds must not create unbounded holes or a session-wide stall.

Validate that correction with this delayed-reader case, ordinary/concurrent
loopback repeats, timeout/drop/cancel cases and the unchanged exact media checks.
Any candidate reference fix needs its own isolated source and explicit provenance;
it cannot be called unmodified-pinned conformance. No compatibility pin change,
upstream source patch or remote issue/PR is made by this checkpoint.

## Reproduction

From the library directory, build using the existing pinned toolchain, then run:

```sh
python3 tests/interop/build.py
python3 tests/interop/reproduce_terminal.py --client tests/interop/build/client
```

A nonzero exit is the defect, not success. Normal acceptance uses:

```sh
python3 tests/interop/run.py --client tests/interop/build/client
```

See [results.json](results.json) and [source-snapshot.json](source-snapshot.json).
Test payloads are synthetic; certificates are ephemeral loopback fixtures. No
device keys, provider data, microphone samples or private deployment logs are
included. Earlier physical heap/audio evidence remains valid for its stated
scope; this diagnosis does not close speech, TLS/native allocation, UI/app or
release-soak gates.
