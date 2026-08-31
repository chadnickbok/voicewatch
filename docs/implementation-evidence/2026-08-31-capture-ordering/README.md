# Capture ordering and publisher throughput checkpoint

Recorded 2026-08-31. This is an intermediate checkpoint, not full-shell or
provider-turn acceptance. No default firmware restoration is required.

The native capture worker now reads groups in arrival order and buffers at most
32 group handles, with a 500 ms missing-group deadline. Six regression tests
exercise ordering, exact PCM/tails, missing groups, range limits, cancellation,
and late control boundaries. Missing groups still terminate capture; this does
not implement packet-loss concealment or cap upstream cache allocation.

The C publisher previously sent at most one group per owner poll. A regression
with three audio groups per 60 ms poll reproduced unsent speech falling behind
the 200 ms cache lifetime. Publication now catches up in bounded batches of
four, stopping for backpressure, control work or lack of progress. The seven C
host test programs and thirteen Rust tests pass. The updated Ultra application
build also passes, but the catch-up fix has not yet been flashed or tested on
hardware.

## Physical results before the publisher fix

`hardware-results.json` contains selected result fields. Event files contain
fixed event names and numeric measurements; native logs contain only restricted
diagnostic categories and transport counters. Credentials, firmware images,
raw serial/provider logs and ambient PCM are excluded.

- `audio3`: 19,200 microphone samples and 16,037 speaker samples passed, including
  response cancellation/replacement and lease-expiry reconnection.
- `audio5-long`: the same sequence passed with 48,000 microphone samples (3 s).
- `audio4-ui` and `audio6-ui`: requesting the listening shell during capture
  caused missing-group failures. These runs do not pass voice UI acceptance.
- `audio7-perf-ui`: an optimized firmware build still failed, at group 44 with
  later groups buffered. Compiler optimization alone did not resolve capture.
- `p3` through `p6`: real-provider attempts failed before a complete voice turn.
  `provider_calls` records the enabled test mode, not successful provider work.
- `flash5`, `flash6` and `flash7-perf`: app0-only writes reached the shell steady
  state. These results establish boot success, not media acceptance.

The latest installed image is `flash7-perf`, SHA-256
`6d2bdacb14e4887e75690a115cbd5bc2f4abe4b822b0e40b96c1db6ebc4d4c0d`.
Its private configuration enables performance optimization; the repository's
default compiler profile has not been changed. Tests use host-driven capture,
so physical button/PTT behavior remains unverified.

Next: flash the publisher fix, repeat the listening-shell audio bench, then
retry the real STT/model/tool/TTS turn. Production deployment, resource bounds,
renewal, lifecycle hardening and full stress/soak acceptance remain open.
