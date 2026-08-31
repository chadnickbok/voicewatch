# Packet ordering and cancelled-response recovery

Recorded 2026-08-31 on the unchanged flash43 full Ultra shell. This checkpoint
adds bounded UDP reordering/duplication tests and fixes a host conversation
lifecycle defect exposed by a combined fault run. It does not close the complete
WebRTC replacement acceptance gate.

## Speech-failure diagnostic follow-up

The earlier p101 matrix failure remains a failure. Two separate diagnostic
captures use the same fixed six-word synthetic fixture, volume 60, 60 ms added
RTT and frozen scoring policy. p106 adds no loss; p107 adds 3% seeded loss. Both
produce zero-error transcripts, complete their real read-tool/TTS responses and
shut down cleanly. These are additional observations, not replacements for p101.

Bounded microphone PCM is held only in RAM for this optional analysis and then
cleared. Only numeric aggregates are retained. p106/p107 waveform correlations
are 0.372/0.368 and envelope correlations are 0.697/0.712. Tested duration-scaled
variants correlate less well in both captures. Their aligned waveform offsets
are 7,785/7,781 samples; those include the deliberate fixture-start pause and Mac
audio startup, and are **not PTT latency measurements**. Both recorded acoustic
paths contain much less high-frequency energy than the digital source; this is
not evidence isolating a transport or sample-clock defect.

p107 has three lost groups and 960 concealed capture samples (60 ms), versus
p101's seven groups and 2,240 samples (140 ms). A common RNG seed does not give
identical packet-loss placement across independent QUIC connections. The
diagnostics do not reproduce p101's error or establish its cause. The original
twelve-cell speech matrix therefore remains unpassed.

## Bounded packet fixture

Every original/copy shares a 256-packet pending limit; each packet is at most
2,048 bytes. Configuration is bounded, pressure invalidates a bench, closing
cancels pending payload timers, and delayed replies retain their original client
UDP port. Reordering delays every eighth received surviving packet; duplication
resends every selected surviving packet byte-for-byte after 5 ms. Both directions
must demonstrate actual delivery of the requested faults. QUIC remains encrypted;
WSS, enrollment, grants and certificate checks are unchanged.

All runs below have 60 ms **added** RTT, not measured total RTT. Existing Wi-Fi
jitter remains present. The packet cases inject no loss. Each successful ordinary
case requires three current provider turns and a fresh idle reconnect. The speech
policy is unchanged: every admitted completion must preserve the exact critical
phrase and meet the existing word-error limit.

| Run | Additional fault | Outcome | Complete responses | Downlink PLC / late / silence |
| --- | --- | --- | --- | --- |
| p108 | Every eighth packet delayed 80 ms | PASS; three zero-error turns | 3, 128,290 samples | 49 / 49 / 0 |
| p109 | Every seventh packet duplicated after 5 ms | PASS; three zero-error turns | 3, 118,606 samples | 0 / 0 / 0 |
| p110 | Every eighth delayed 250 ms; every sixteenth duplicated; delayed-TTS cancellation | FAIL; one fresh turn completes, next response cancels and remains Speaking | 1, 42,360 samples | 9 / 8 / 0 on the completed response |
| p111 | First media pump paused 350 ms after eight packets | PASS for deliberate cancellation/recovery, then three zero-error turns | 3, 123,447 samples | 0 / 0 / 0 on complete responses |
| p112 | Same packet profile and delayed-TTS cancellation as p110, after the fix | FAIL; first fresh transcript changes the critical phrase; its response finishes and returns Ready | 1, 47,201 samples | 8 / 8 / 0 |

p108 records 154/165 actually reordered uplink/downlink packets; p109 records
194/166 delivered copies. Exact firmware speaker totals match each completed
host response. This checks response boundaries and history ownership, not
subjective speaker audibility. Concealment and late counts may overlap and are
not independent lost-frame totals. Nonzero counters alone do not fail readiness.

## Confirmed lifecycle defect and fix

p110 successfully rejects delayed TTS from an explicitly cancelled turn. Its
second fresh response then exceeds the existing 200 ms host pacing-debt limit.
The transport cancels playback, but the sink's failed-drain path only discards
pending text: it leaves TTS active and the conversation in Speaking. The bench
times out waiting for a complete response. The failed result is preserved, with
clean service shutdown. The original pacing stall's cause is not yet isolated;
packet reordering alone does not establish host-pump scheduling causality.

The sink now ends its own failed utterance and bot-speaking state without
committing unheard text or invoking the successful natural-pause path. An owned
failure callback invalidates that provider turn, queues interruption and returns
the conversation to Ready. Ownership and generation checks prevent an old drain
or an asynchronous callback from resetting a replacement turn. Durable attention
items remain pending for another authorized delivery attempt.

p111 deliberately suspends only the media pump for 350 ms after eight packets.
The actual pacing-overrun path fires; its response is cancelled, has no completed
speaker receipt, and adds no spoken history. Failure handling starts 0.836 ms
after the pacing-overrun trace event. This is host trace spacing, not a measured
physical UI latency. Three subsequent same-session voice turns complete with
exact 41,149-sample receipts each, followed by idle reconnect and clean shutdown.
The 2,560 samples submitted from the cancelled response are not counted as a
completed response or asserted to have all played on the speaker.

Neither the 200 ms pacing bound nor the watch's prebuffer, queue, loss budgets or
speech thresholds are relaxed. The controlled fault verifies recovery; it does
not prove that an arbitrary impaired response always finishes.

p112 repeats the combined packet profile after the fix and again rejects the
explicitly cancelled turn's late TTS. Its first fresh transcript has one word
error that breaks the required critical phrase, and the expected fresh read-tool
call is absent. The resulting response nevertheless finishes all 47,201 samples
and returns Ready; there is no pacing overrun. The bench correctly fails and
stops before the remaining turns/reconnect, then shuts down cleanly. It records
87/88 reordered uplink/downlink packets and 44 copies each way, with no fixture
pressure. This is neither a passing combined case nor a reproduction of the
original pacing stall. No additional retry replaces either failed observation.

## Provenance and remaining acceptance

`hardware-results.json` contains whitelisted numeric/boolean results, fixed
status labels, exact source references and private-log hashes. The source
snapshots separate acoustic diagnostics, the original packet runs and the
recovery implementation. `firmware.json` retains flash43 provenance. Private
keys, endpoint profiles, databases, raw traces and recordings are not included.
Ambient microphone audio is never written to disk.

All seven private hosts start and shut down cleanly with no component timeout.
Completed responses have matching host/firmware sample totals; partial cancelled
output is excluded. The largest recorded ngtcp2 peak is 37,960 bytes against the
existing 131,072-byte cap, with no recorded firmware crash markers. This does not
establish the outstanding TLS or total-device memory bound.

Before the recovery fix, the focused regression selection records three failures
and three passes. The sink/ownership/conversation group then passes 67 tests. The
final full Python suite passes 384 tests with four warnings, and the explicit
native integration/firmware protocol group passes 46 tests. UDP fixture tests
cover actual bidirectional order and byte-exact copies, shared pending capacity,
close cleanup, client-port ownership and invalid profiles. Counts and log hashes
are in `verification.json`.

Permanent enrollment is reapplied at revision 164, with a fresh permanent-host
session-ready event verified after installation. The existing MoQ supervisor,
child and legacy service processes are unchanged. Flash43 remains installed;
the new local host recovery code has not been deployed to the permanent service.
All current changes remain uncommitted.

These are UDP/QUIC packet tests, not proof of delayed application-group
ownership or a deliberately flow-control-blocked stream. Physical PTT/UI/apps/
packages/sleep-wake, calibrated latency, TLS/total-memory allocation bounds,
reference interoperability, sanitizers/CI and the original endurance/release
matrix remain open. No library reference pin, production host, default transport
or firmware image is changed by these runs. Firmware restoration is not required.
