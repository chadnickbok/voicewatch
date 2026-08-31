# Capture ordering and publisher throughput checkpoint

Recorded 2026-08-31. This is an intermediate checkpoint, not complete full-shell or
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
build also passes. The follow-up hardware results below validate the catch-up
fix in the listening shell; real-provider voice-turn acceptance remains open.

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

The pre-fix optimized image was `flash7-perf`, SHA-256
`6d2bdacb14e4887e75690a115cbd5bc2f4abe4b822b0e40b96c1db6ebc4d4c0d`.
Its private configuration enables performance optimization; the repository's
default compiler profile has not been changed. Tests use host-driven capture,
so physical button/PTT behavior remains unverified.

## Follow-up: listening shell and repeated capture pass

`flash8-catchup` includes the publisher catch-up fix. `audio8-catchup-ui` failed
to start capture; `audio9-catchup-ui` passed 48,000 microphone samples with the
listening UI, 16,037 playback samples, cancellation/replacement and lease-expiry
reconnection. This exposed an independent owner race: a replacement start could
be dequeued before cancellation cleanup applied its revision, then erased by
that cleanup. Dequeue now checks the applied revision under the producer mutex.
`flash9-start-race` contains that correction.

The Ultra touch decoder also now rejects idle/ACK reports before interpreting
the palm bit. The older implementation could treat stale gesture payload as a
voice-cancel request. The additional source pin is recorded in the library's
hardware lock; [SensorLib's validated report handling](https://github.com/lewisxhe/SensorLib/blob/2b9e591f245e447d3d00ec8798c3f49b897882d9/src/touch/TouchDrvCST92xx.cpp#L63)
is the authority. A portable test covers valid contacts/releases, bounds, valid
cover gestures, and stale palm bits in idle/ACK/invalid reports. It runs in the
library's GCC/Clang host CI. Physical touch and palm gestures remain unverified.

`flash10-touch` contains the publisher, owner-race and touch fixes. On that image,
`audio10-twenty` passes **20 consecutive one-second captures / 320,000 microphone
samples**, with listening UI active throughout, followed by exact-tail playback,
response cancellation/replacement and a fresh-grant lease-expiry reconnect.
Forced reconnect took 5,226 ms. This is a limited lifecycle check, not the required
1,000-PTT or endurance matrix.

## Real provider path: still incomplete

`p7` captured audio but the Mac fixture output was muted. Subsequent benches
temporarily set an explicit output volume and restore the original setting,
including on failure. Results contain only level statistics, stage counters and
fixed events, never microphone recordings or transcripts.

`p8`–`p11` exposed incomplete capture/cancellation and an early VAD commit followed
by an empty transcript. The MoQ provider pipeline now uses explicit capture
start/end frames in place of local VAD. The end is queued only after the native
capture terminal and sample count validate. A new start clears abandoned STT
input; cancelled captures do not intentionally commit. WebRTC retains its
existing VAD and end padding. Three new tests cover actual pinned processor queue
ordering, exact PCM/tails, cancellation and clearing before a new capture.
This does not yet complete provider generation isolation within a session.

`p13` proves the explicit commit occurs after validated capture, but STT returns
an empty transcript. A separate synthetic-only probe sends the same generated
fixture directly to the same STT service: it returns 33 characters and recognizes
the exercise request. This isolates provider availability; it is not physical
voice-turn acceptance. `p14`/`p15` level measurements show a clipped startup sample
and much quieter later audio, and still expose intermittent session retirement
near capture completion. The physical acoustic capture path and terminal failure
need further diagnosis. No real STT/model/tool/TTS turn has passed.

`flash11-control-site` adds a numeric control-retirement location diagnostic and
passes app0-only flash/boot validation. It is the current installed image, SHA-256
`6754bc2ebbd7c39e81016eb48fbdfb569684e7abdf984694bbed4fa92cf74171`.
On that image, `p16` receives 38,184 microphone samples before media failure
result 12; control retirement site 1548 is the media-error handler in
`voice_service.cpp`. This shows control closes in response to media failure;
the originating transport failure is still unresolved. Native diagnostics report
only `other session failure`, and no STT commit completes in this run.

The full Python suite passes 199 tests with existing dependency/runtime warnings;
13 Rust tests, the seven C host programs, the touch test and the separate native
integration lane pass. Production deployment, allocation bounds, proactive
renewal, within-session lifecycle hardening, physical UI controls and full
stress/soak acceptance remain open. No default firmware restoration is required.
