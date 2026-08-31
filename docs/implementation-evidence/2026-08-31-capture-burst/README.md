# Capture burst recovery and transport diagnostics

Recorded 2026-08-31 after VoiceWatch `7947623` and moq-esp32 `f5abf7a`.
This checkpoint remains partial implementation evidence, not replacement readiness.

The native capture worker could reject a healthy queued burst behind one missing
group before its 200 ms gap timer ran. A deterministic pinned-track regression
reproduced `capture reorder bound`. The worker now retires only a bounded missing
prefix and drains retained groups before taking more handles. Storage remains
bounded to 32 handles and concealment to 200 ms per gap. The regression now
returns exactly 12,807 samples with one lost group and 320 PLC samples.

Flash21 adds numeric first-failure diagnostics across the QUIC adapter, endpoint
and firmware. It does not fix or explain the original p50 transport error. That
error did not recur in the five subsequent runs below. The native-only bench now
supports the same seeded UDP impairment proxy and checks playback completion
booleans. WSS remains unimpaired.

| Run | Impairment | Result | Scope and limits |
| --- | --- | --- | --- |
| p51 | 3% loss, 60 ms added RTT | Fail | Third provider turn omitted the required fresh watch-state read; all three playback counts matched. |
| p52 | 5% loss, 120 ms added RTT | Fail | Same missing read; playback pressure reached 108. |
| t53 | 3% loss, 60 ms added RTT | Fail | Before burst fix: 19 of 20 captures, then native reorder-bound rejection. |
| t54 | 3% loss, 60 ms added RTT | Pass | After fix: 20 captures of 1.2 seconds, tone, cancel/replace, forced reconnect and expiry reconnect. Replacement playback pressure was 41. |
| t55 | 3% loss, 60 ms added RTT | Pass | After fix: three captures of 10 seconds, tone, cancel/replace, forced reconnect and expiry reconnect. Replacement playback pressure was 32. |

t54 and t55 captured 384,000 and 480,000 samples respectively. Each played two
completed 16,037-sample tones. These are repeated captures followed by playback,
not 23 complete PTT/echo cycles. Exact counts do not establish speech quality;
the pressure counters leave that gate open. The seeded tests are not identical
packet-schedule replays. Neither physical run logged the newly instrumented
native pressure branch, so the deterministic regression supplies direct evidence
for that correction. Expiry reconnect is not proactive lease renewal.

Verification passes 22 Rust tests, Clippy, 299 Python tests (four warnings), six
native integration and 26 firmware-parser cases, the ESP-IDF build, and adapter
and host C tests under both normal execution and Linux ASan/UBSan. Playback tests
and earlier audio sanitizer evidence are retained in the sibling
[playback checkpoint](../2026-08-31-playback-timeline/README.md).

`hardware-results.json` contains whitelisted counters and numeric log extracts.
Source snapshots preserve the pre-burst diagnostic runtime and t54/t55 runtime;
later documentation edits do not change those historical hashes. No credentials,
raw logs, transcripts or ambient microphone PCM are included. Flash21 remains
installed; default firmware restoration is not required. The full acceptance
plan, unresolved transport failure, provider read behavior and speech-quality
work remain open. WebRTC remains the configured default.
