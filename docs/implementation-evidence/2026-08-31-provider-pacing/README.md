# Provider speech, pacing and service startup ownership

Recorded 2026-08-31 against Voicewatch base `fdeb92f` and moq-esp32 base
`7101187`, with the dirty source versions recorded alongside these results.
Flash43 remains installed: the complete secure MoQ Ultra shell with the 80 ms
startup prebuffer. This checkpoint writes no firmware and does not update either
persistent host. It advances, but does not complete, the replacement plan.

## Host lifecycle defect and correction

Initial p87–p90 complete their provider/audio assertions, but none emits the
service's full-startup banner or shutdown trace. The bench originally treated
the listening transport as service readiness and ignored exceptions returned by
its final gather. p90 writes its successful provider result after 51,584 ms,
then remains alive in event-loop shutdown. A process sample confirms it is still
running; its result file is not evidence of process completion. The matrix
driver is explicitly terminated, and its child is subsequently absent. No later
matrix cell starts. These old results remain narrow provider/audio observations,
not verified service lifecycle passes.

Two deterministic reproductions fail before the correction: discovery
registration failure leaves its Zeroconf owner open, and failure after transport
startup skips transport/database cleanup. The old physical bench discarded the
original startup exception, so its precise exception and the remaining task that
held p90 open cannot be recovered. Do not claim that a particular mDNS collision
was observed. The reproductions establish the cleanup defect independently.

Startup now shares the service's cleanup scope. Asynchronous Zeroconf
registration owns its resources across rejection/cancellation and awaits its
announcement before reporting readiness. Normal deployments still advertise by
default. The new `serve --no-discovery` option is used by the private bench,
whose endpoints are already explicitly enrolled; it must not compete with the
permanent service's advertisement. The bench now requires `service.ready`, a
live service task before shutdown, no returned service exception, and
`shutdown.completed`. Readiness/cancellation, registration announcement, and
partial transport startup have additional regression coverage.

## Physical results

Every run uses the real provider pipeline, the native Rust endpoint, authenticated
WSS/QUIC, and the physical Ultra microphone/speaker. Captures are host-driven,
not physical button presses. The fixed Mac-spoken six-word fixture is scored
under the unchanged speech policy. Tools are read-only, and the optional
background event exists only in the isolated test database.

| Run | Scenario | Result and limits |
| --- | --- | --- |
| p87 | Text, background, then three voice turns; no injected impairment | Provider checks pass; full service lifecycle unverified. One concealed/late downlink chunk. Fixture volume 70. |
| p88 | Cancel held real TTS, release during replacement, then three voice turns | Provider/cancellation checks pass; full service lifecycle unverified. |
| p89 | 5% loss, 120 ms added RTT, 800 ms uplink outage, then three turns | Provider/recovery checks pass; full service lifecycle unverified. Recovery 1,087 ms; 16 concealed, 28 late and two fallback-silence chunks. |
| p90 | First additional matrix cell: 0% loss, 30 ms added RTT | Three provider turns pass, but process shutdown hangs; matrix attempt stopped. |
| p91 | Corrected service/bench; text, background, three voice turns | PASS: five full responses and history additions; zero fixture word errors; startup, reconnect and shutdown verified. |
| p92 | Corrected service/bench; delayed TTS cancellation, three fresh turns | PASS: retired TTS rejected, three fresh full responses, zero word errors; startup/reconnect/shutdown verified. |
| p93 | Corrected service/bench; 5% loss, 120 ms added RTT, 800 ms outage | PASS: failed capture aborted without STT commit, same-session recovery in 973 ms, three zero-error turns and full responses; startup/reconnect/shutdown verified. |

All runs except p87 use fixture volume 60; Mac volume/mute is restored afterward.
Final p91–p93 deliver **nine completed voice turns and two output-only responses**,
totalling **445,379 played samples**. Independent firmware sample totals match
host receipts in order. Output-only speech produces no microphone samples or STT
commits. Each reconnect remains idle without starting capture. All three final
runs have explicit startup/shutdown traces, no component shutdown timeout, no
host pacing overrun, and exit status zero.

Final clean/cancellation playback has zero PLC, late, pressure or fallback-silence
counts. Under impairment p93 records 22 concealed and 22 late chunks, with zero
queue pressure and fallback silence. These counters may describe the same frames;
they are not added as independent losses or treated as automatic defects.
The measured uplink fixture score does not establish downlink audibility or
general speech quality. No physical speaker waveform is captured here.

The recorded ngtcp2 allocator peak is 30,344 bytes in p91/p92 and 48,864 bytes
in p93, against the existing 131,072-byte cap, with no allocator denials. These
snapshots exclude TLS and total-device allocation; they do not close the full
memory gate. No firmware crash/watchdog marker appears in the captured windows.

## Verification and retained scope

The final complete Python suite passes 369 tests with four warnings. Six real
native integration cases and the explicit firmware protocol cases also pass;
exact counts and log hashes are in `verification.json`. The prior C/audio/UBSan
and ten-minute pacing evidence is linked from the progress report, not rerun or
relabelled here. ASan, remote CI and clean release validation remain open.

`source-snapshot.json` identifies p87–p90. `lifecycle-source-snapshot.json`
identifies p91–p93 and the final regression tests. `hardware-results.json`
contains numeric whitelisted observations and private-log hashes, with a separate
service-lifecycle verdict so the historical `pass_` fields cannot be mistaken
for full-service acceptance. Raw logs, credentials, databases and synthetic audio
remain private. Ambient microphone PCM is never persisted.

Permanent enrollment is reapplied at revision **143**, with a fresh media-ready
event observed afterward. The original MoQ supervisor/child and legacy service
processes remain running. No firmware restoration is required or performed.

The full loss/delay matrix has **not** passed: its expansion stopped at p90, and
only the most impaired cell was repeated with the corrected lifecycle. Reorder,
duplicates, blocked-stream cases, broader speech/downlink quality, physical
PTT/navigation/apps/packages/sleep-wake, full memory/security negatives, latency,
stream turnover, 1,000 cycles, eight-hour soak, unchanged-reference compatibility,
release/CI and deployment/default-switch gates remain open. WebRTC remains the
configured default. The next work is to resume that matrix with full lifecycle
checks, then advance the remaining physical product gates.
