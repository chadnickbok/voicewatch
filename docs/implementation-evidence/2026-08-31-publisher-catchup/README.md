# Publisher catch-up and repeated capture checkpoint

Recorded 2026-08-31. This checkpoint adds publisher loss diagnostics, fixes a
reproduced scheduling shortfall, and validates the updated full-shell firmware
on the Ultra. It does not establish release readiness or conclusively explain
the earlier intermittent missing-group failure.

## Change and regression

At 50 audio groups per second, a 160 ms network-owner pause can accumulate eight
groups. The previous four-group per-poll limit leaves part of that backlog even
when the existing transmit pool has capacity. The regression fails with that
limit and passes when polling can fill the existing bounded TX slots. Tests
cover both three groups per 60 ms and eight groups per 160 ms over 30 batches.
Control reserve, queue sizes, backpressure and live-media deadlines are unchanged.
This change does not recover a group that has already expired.

Engine statistics now distinguish cache-drop ranges from enqueued transmit
expiry, failure and cancellation, and count backend submission and retirement.
The network owner copies these into the locked service statistics; firmware
logs numeric snapshots at capture completion or failure. Counts describe
outgoing subscription attempts per connection, not unique samples or remote
playback. Tests separately induce cache loss and transmit expiry.

## Hardware evidence

| Run | Firmware | Outcome |
| --- | --- | --- |
| p28 | flash17, diagnostics with the previous batch limit | Three provider turns and fresh-session reconnect pass; 180,640 microphone samples and 127,079 speaker samples |
| p29 | flash18, diagnostics and bounded batch fix | Three provider turns and fresh-session reconnect pass; 181,600 microphone samples and 124,659 speaker samples |
| audio12-hundred | flash18 | 100 one-second captures pass with the listening UI active; 1,600,000 microphone samples, followed by exact 16,037-sample playback, cancellation/replacement and lease reconnect |

Each provider turn requires a fresh STT completion, recognized synthetic
fixture, actual read-only exercise tool completion, and a newer response's
watch speaker receipt. The provider acknowledges disabled noise reduction.
Capture initiation is host-driven; these runs do not test physical PTT buttons.

All 100 repeated captures report 16,000 accepted samples, no discarded input,
no dropped chunks and one initial codec reset. The final publisher snapshot
reports zero cache drops, expiries or failures, and eight cancellations during
the run's subscription/lifecycle transitions. Cancellations are not presented
as zero. The fresh-lease reconnect takes 5,371 ms; total test duration is
235,393 ms. This is 100 captures followed by playback, not 100 complete echo
cycles, and does not satisfy the 1,000-cycle or eight-hour gates.

Both images reached the full-shell steady-state marker after app0-only flashing
at offset 0x10000. No default firmware preservation or restoration is required.
Firmware, enrollment credentials and raw device logs remain private.

- flash17 SHA-256: `8e94382bb145ff0fc0e5711ae14ad33545a91372565341afcafc73db60f5bf63`
- flash18 SHA-256: `9126220122365874058091c140e33d6c1a5954dabe125b8c1b89c5166155e308`

## Verification and artifact scope

Seven C host programs pass on macOS. The same seven programs also pass normally
and with ASan/UBSan in an isolated Linux container with networking disabled,
no host mounts, dropped capabilities and bounded resources. The exact image
digest and source archive hash are in `sanitizer-environment.json`; compiler
commands and results are in `linux-host-tests.log`. The macOS sanitizer attempt
stalled during runtime initialization before main and was terminated; it is not
counted as a passing sanitizer run. Adapter and audio suites, 209 Python tests
and five native integration cases also passed during this checkpoint.

Public artifacts contain fixed event names, numeric counters, fixture booleans
and sanitized test output. They contain no captured microphone PCM, actual
transcripts, provider credentials, enrollment secrets or raw serial/provider
logs. `source-snapshot.json` records the source and evidence hashes. The earlier
provider-turn snapshot remains historical and refers to its earlier image and
source state.

## Remaining limits

The [p25 missing-group failure](../2026-08-31-provider-turns/README.md) has not
recurred with the new diagnostics, so its original cause remains unconfirmed.
The deterministic scheduling fix and successful repetitions narrow that
uncertainty; they do not replace controlled loss/recovery tests. Capture/provider
generation isolation, long responses and lease renewal, hard allocation limits,
security coverage, service deployment, physical full-shell interaction and the
remaining endurance gates are still open. WebRTC remains the default transport.
