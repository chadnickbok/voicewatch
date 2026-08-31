# Authenticated playback timelines and controlled loss

Recorded 2026-08-31 after VoiceWatch `7947623` and moq-esp32 `f5abf7a`.
The implementation goal remains the complete WebRTC replacement plan. This is
partial acceptance evidence, not a release-candidate or full-shell sign-off.

## Reproduced defect and correction

The player previously chose the first received frame timestamp as time zero.
An isolated real-Opus regression drops the first 20 ms packet of a 1,607-sample
response and reproduces 1,287 output samples: exactly 320 short. This mechanism
is consistent with p47's physical 47,201 versus 46,881 sample mismatch, though
the old run did not retain packet-level evidence proving which packet was late.

Native `playback.prepared` now carries `pts_us`, the encoder's exact response
epoch. The host validates it and forwards it in authenticated WSS
`playback.begin`. The firmware sets that epoch before consuming media. Missing
initial audio no longer shifts time zero, and PLC consumes pre-skip exactly
once. Host, native endpoint and firmware require a coordinated update.

The authenticated `playback.end` sample count also reaches the audio owner.
Drainage waits for this boundary as well as receive completion. A missing tail
can use at most 200 ms of concealment; excessive tails and conflicting Hang
terminal markers fail. This does not relax the host's exact speaker receipt.
The player uses fixed storage and still bounds PLC before falling back to
silence. Existing cancellation, queue pressure and stale-playback behavior
remain covered by the audio suite.

## Validation

The audio regression now produces 1,607 samples. Twelve combinations of leading
packet, initial reset and tail loss match independent direct Opus decoding/PLC
sample for sample. Additional cases cover an entirely lost bounded response,
zero length, an excessive tail, conflicting end metadata and cancelled state.
Nine pinned-reference decode cases and twelve encoder fixtures still pass.

299 Python tests pass with four existing warnings. Six native integration and
26 firmware-parser cases pass; 21 Rust tests, the locked offline native build,
Clippy with warnings denied and the ESP-IDF firmware build pass. Normal audio
tests and isolated Linux AddressSanitizer/UndefinedBehaviorSanitizer runs pass.
The macOS ASan runtime stalled before `main` and was stopped after sampling its
initialization deadlock; it is not counted as a passing sanitizer run.

## Physical runs

| Run | Firmware / condition | Outcome |
| --- | --- | --- |
| p45 | Previous firmware; new capture counters | Failed on a closed-schema mismatch before STT commit; subsequently fixed |
| p46 | Previous firmware; loss-free capture recovery baseline | Text, background, three voice turns and reconnect pass |
| p47 | Previous firmware; 1% loss, 30 ms added RTT, seed 47 | Two turns pass; third playback is 320 samples short and correctly rejected |
| p48 | Flash20; no induced impairment | Text, background, three voice turns and reconnect pass |
| p49 | Flash20; 1% loss, 30 ms added RTT, seed 47 | Three voice turns and reconnect pass; exact played sample counts despite six concealed/late frames |
| p50 | Flash20; 3% loss, 60 ms added RTT, seed 50 | Transport failure during first capture; no STT commit or playback |

p48/p49 together complete eight responses, six microphone/provider/tool turns
and two fresh-session reconnects. They record 360,640 microphone samples and
318,301 played samples. All host/watch playback totals match. p48 reports zero
concealed/late/pressure frames and zero host capture recovery. These counters
are not a calibrated acoustic speech-quality measurement.

The impairment fixture drops encrypted datagrams independently in both
directions and splits the specified **added** RTT equally between them. Actual
RTT includes the underlying network and is separately retained where available.
It does not impair WSS. Seed equality does not imply the same packet schedule:
provider output, timing, packetization and QUIC state vary between runs.

p50 delivered 48,216 capture samples to the host before retiring. The watch
reported media error 12 (`ESP_MOQ_ERR_TRANSPORT`), one expired publisher group,
zero microphone drops and a 145 ms maximum network poll gap. Catalog withdrawal
was the host's final reported cause. These observations do not yet prove which
transport path caused the failure. Investigating that failure is next; the
5%/120 ms test and the full loss/reorder/burst/quality matrix remain unfinished.

## Provenance and privacy

`hardware-results.json` retains failed attempts and numeric watch/native
counters. Source snapshots distinguish the pre-fix host, physical-run sources
and final source/test revisions. `firmware.json` identifies flash20 and its
app0-only write; bootloader, partition table and OTA selection were not changed.
The new firmware remains installed. Enrollment uses only the dedicated test
namespace; no NVS erase or default-firmware restoration is required.

Credentials, private profiles/certificates, databases, provider and serial logs,
and fixture audio remain outside the repository. Ambient microphone PCM is not
persisted. Only aggregate counters and fixed diagnostic labels are published.
Mac output was verified restored to volume zero and muted. Bench child services
stop after each run; this is not a persistent service deployment.

Long responses/lease renewal, hard native allocation bounds, broader security,
physical controls/apps/sleep-wake, latency, endurance, deployment/default switch
and the remaining nine-finding audit still require their own evidence.
