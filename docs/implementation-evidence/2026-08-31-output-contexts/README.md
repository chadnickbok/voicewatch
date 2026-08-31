# Watch-owned output-only response contexts

Recorded 2026-08-31, following VoiceWatch `dfde572` and moq-esp32 `1769b70`.
The new full-shell MoQ firmware was written to app0 only and booted successfully.
Its identity is in `firmware.json`. No bootloader, partition table, OTA selection,
NVS erase, default-firmware backup or restoration was performed.

## Behavior and corrections

Text and idle background speech obtain a watch-issued response context before
output begins. The native worker creates no microphone reader for these
contexts. They share the watch's monotonic media identity namespace with real
captures; legacy IPC field naming does not imply a fabricated capture.
Late acknowledgements, busy responses, cancellation and replacement have
conversation/session tests. Text is marked separately from STT in telemetry.

The first physical attempt, p41, stopped before enrollment because its output
directory made the Unix socket path 101 bytes long. The bridge correctly refused
it, but the bench hid the startup error behind a readiness timeout. The bench
now checks the path limit and observes service startup failure directly.

In p42, text speech passed with zero microphone activity. The background speech
also played 21,784 samples, but the history gate failed. The pinned TTS service
emits a standalone speech history-flush frame before its stop frame. Our sink
held spoken words until playback but let the flush pass early, producing an
empty history commit. Two tests reproduce the ordering failure. The sink now
holds the flush behind the words and the speaker receipt; cancellation discards
both. Both regression cases pass.

Durable attention selection no longer consumes MoQ announcements or focuses
questions before playback. A successful, current speaker receipt acknowledges
the action. Cancelled/replaced transport drains explicitly report unsuccessful
playout. Tests verify delayed receipts, cancellation, transport cancellation,
device scoping and a pending announcement surviving a broker/database restart.
This is at-least-once delivery: a process crash after audible playback but before
the database acknowledgement can still cause a retry.

## Physical evidence

The bench uses authenticated WSS/native QUIC, the real model and TTS providers,
read-only watch tools, and the physical Ultra speaker. Microphone turns use a
fixed Mac-spoken phrase near the watch, the real STT provider and the same live
conversation pipeline. Text enters the existing application event handler from
the host driver. Background completion is a deliberately created test job in an
isolated database; no external job worker is launched.

| Run | Scenario | Result |
| --- | --- | --- |
| p41 | Initial long output-directory path | Startup failure; no enrollment or audio |
| p42 | Text then background, before history fix | Text passed; background played but failed history acceptance |
| p43 | Text and background before any capture, then three voice turns | Five played responses, five history additions, reconnect; pass |
| p44 | Background TTS held and cancelled before audio, then idle retry and three voice turns | Pending event retained; old frames released during a new context; four played responses/history additions, reconnect; pass |

The two successful runs deliver three output-only responses, six complete
microphone/STT/tool/TTS turns and two fresh-session reconnects. They record
364,000 microphone samples and 346,136 played samples. Each output-only case
records zero microphone samples, zero STT commits and zero capture-start events.
Both reconnects remain idle without activating the microphone. Independent
firmware capture/playout totals match the host response records exactly.
These are timeline/sample-count checks, not bit-for-bit acoustic comparison.
p44's first microphone response reports three late/concealed frames; that
observation is retained and is not a controlled packet-loss acceptance test.

p44 starts with no preceding text or capture. It holds real TTS start/audio,
cancels the owning turn, checks that the announcement is still pending, and
lets the production idle loop obtain another watch context. Old frames released
into that replacement do not add playback or history. The retry adds exactly
one played assistant message and consumes the pending announcement once.

The final Python suite passes 284 tests with four warnings; six native
integration cases pass. The firmware build/boot passed, and the native lane
includes exact reference-codec comparison for output-only speech. Per-run
source hashes distinguish p42 from the corrected p43/p44; the final bench also
adds a CLI guard against combining text-first with the background-first fault.
Production runtime sources are identical between successful runs and the final
source snapshot. These tests do not eliminate the existing suite warnings.

## Privacy and limits

Public files contain fixed stage names, numeric counters, booleans and source
hashes. Raw serial/provider logs, keys, certificates, profiles, databases and
fixture audio remain private. Ambient microphone PCM is never persisted.
Mac output is restored after fixture playback; watch firmware restoration is
not required. The new firmware remains installed.

This proves the tested output-only and cancellation paths, not the complete
WebRTC replacement. Physical buttons/touch/navigation, installed Wasm apps,
package delivery and sleep/wake still need full-shell acceptance. Controlled
packet-loss recovery, continuous long responses and lease renewal, hard native
allocation caps, broader adversarial security, production supervision/deployment,
latency targets and endurance remain open. WebRTC remains the configured default.
