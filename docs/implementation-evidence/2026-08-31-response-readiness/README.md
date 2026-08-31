# Response subscription readiness and startup audio pressure

Recorded 2026-08-31 after VoiceWatch `a924616` and moq-esp32 `7555add`.
The full replacement plan remains open; these are targeted regression results.

## Reproduction and correction

Flash22 adds bounded numeric diagnostics: subscription readiness, the first
three audio arrivals, the first pressure event and final silence counters. It
does not change playback behavior. In t56, the first response reaches
`MEDIA_READY` at 304,887 microseconds, then receives packets 1 through 11 within
6,661 microseconds. No samples have played before that first queue overflow.
Although the host paces PCM, it starts before the media subscription is ready;
the subscription delivers the accumulated audio as a burst. Evicting older
queued packets then keeps depriving playback of its next due packet.

t56 reports exact 16,037-sample completions but pressure 41/36 and silence 38/30
chunks for its two tones. Its existing control/count bench passes. It does not
meet audio-quality acceptance. This is why counts alone are insufficient.

The pinned Lite05 publisher sends `SUBSCRIBE_START` only after its first group
exists (`rs/moq-net/src/lite/publisher.rs` at pinned revision
`eb5776e21eeaecba8e844be53c821895c178bcaf`). Waiting for readiness while withholding
all groups would deadlock. The native endpoint now primes only the standard
empty codec-reset group during response preparation. The watch acknowledges
`playback.bound` after `MEDIA_READY` initializes the player. Only then does the
host send paced PCM. No PCM or encoded audio is accepted before binding; an
unbound cancellation consumes its reset group ID without flushing audio.

Flash23 includes the correction and the same diagnostics. This requires native
endpoint and firmware updates together. The player algorithm, ten-packet arena,
60 ms prebuffer, 200 ms loss bounds and codec configuration are unchanged.

## Physical results

| Run | Profile | Result |
| --- | --- | --- |
| t56 | Before fix; 3% loss, 60 ms added RTT, seed 50 | Count/control pass, but pressure 41/36 and silence 38/30. |
| t57 | After fix; same impairment parameters | Two exact tones, zero pressure, silence, concealment and late packets. |
| t58 | After fix; 5% loss, 120 ms added RTT, seed 52 | Two exact tones, zero pressure/silence, three concealed and three late packets per tone. Explicit zero-pressure gate passes. |
| p53 | After fix; no induced impairment, real providers | Text speech, background speech and three complete voice/tool/speech turns; all five playbacks have zero pressure, silence, concealment and lateness. |
| p54 | After fix; 5% loss, 120 ms added RTT, seed 52, real providers | Fail during second capture: native `capture loss budget`. First complete speech response has zero pressure/silence and eight concealed/late packets. |

t57's first packets arrive about 20 ms apart. Both corrected native runs also
pass cancellation/replacement, forced reconnect and expiry reconnect. t58
completes three captures before its two tones; it is not three full PTT/echo
cycles. p53 captures 180,640 samples and plays 193,644 samples with five played
history entries, fresh watch-state reads for all voice turns and a reconnect.
Seeded runs are not byte-for-byte replays of the same packet schedule.

p54 completes one 60,480-sample capture and a 33,887-sample speech response,
including the required fresh watch-state read. The next capture fails before STT
commit when a missing span exceeds the existing 200 ms concealment budget. The
host logs `session actor` / `capture loss budget`, with RTT 142,885 microseconds,
cwnd 2,700 bytes and 22 lost packets. No watch endpoint failure diagnostic is
logged. This is a distinct attributed failure, not a reproduction or resolution
of p50's unidentified watch transport error. The loss bound was not increased.
This failed run does not establish recovery or full voice reliability at that
impairment level. Both provider runs restore Mac output to volume zero and mute.

The native bench now records numeric playout counters and optionally enforces
`--max-playout-pressure 0`. Missing completion logs cannot satisfy that gate.
Failed pressure or proxy-capacity gates also produce a nonzero process exit.
This gate detects queue overflow; it is not a calibrated speech-quality score.

## Validation and limits

The new Rust regression fails before the fix because preparation exposes no
reset to establish readiness. It now verifies reset-only preparation, no PCM
before binding, cancellation before binding and successful audio after exactly
one binding. All 23 Rust tests, Clippy, 299 Python tests (four existing warnings),
six native integration cases, 26 firmware-parser cases and the ESP-IDF build
pass. The actual delayed firmware acknowledgement is exercised on the Ultra;
the Rust regression alone does not test that acknowledgement.

`hardware-results.json` holds whitelisted counters; snapshots identify the
diagnostic, corrected and pressure-gated run sources. Historical hashes remain
unchanged by subsequent documentation edits. Credentials, transcripts, raw
logs, provider files and ambient microphone PCM are not included. Firmware is
written only to app0 and left installed; restoration is not required.

The original intermittent p50 transport fault remains unresolved. Repeated
provider omissions of fresh state reads also remain open despite p53 passing.
The p54 capture loss-budget failure requires further investigation and recovery
validation under the existing bounded-media contract.
This does not close the full impairment/speech-quality matrix, sustained-stream
and renewal tests, hard allocation caps, physical shell/apps/sleep-wake checks,
security matrix, deployment/default switch, latency or endurance gates. WebRTC
remains the configured default.
