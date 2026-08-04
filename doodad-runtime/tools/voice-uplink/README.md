# Doodad Echo Bridge

Echo Bridge is the local physical-device conformance harness for the first
voice transport slice:

```text
Mac `say` -> Mac speakers -> CoreS3 microphone -> Opus/WebRTC -> Mac WAV
          -> whisper.cpp -> transcript.final -> voice-notes Wasm
Mac 660 Hz opener + conformance phrase + 880 Hz closer -> Opus/WebRTC
                     -> CoreS3 speaker -> optional Mac capture + analysis
```

The WebSocket carries only bounded, versioned signaling and control envelopes.
Audio uses Opus over 48 kHz RTP with 16 kHz mono PCM at each application edge,
providing wideband speech in a bidirectional WebRTC session. The Mac
advertises `_doodad-voice._tcp.local`; firmware can use a static URL for
diagnostics.

## Run

Enable `DOODAD_VOICE_UPLINK`, configure `DOODAD_WIFI_SSID` and
`DOODAD_WIFI_PASSWORD` in the local ESP-IDF sdkconfig, and optionally set
`DOODAD_VOICE_SIGNALING_URL` for a static diagnostic endpoint. Do not place
credentials in `sdkconfig.defaults`.

```sh
./tools/voice-uplink/setup.sh
./scripts/build-firmware.sh --app voice-notes --show-app
./tools/voice-uplink/run.sh --runs 1
```

On macOS, setup builds a local FFmpeg at the pinned upstream commit that fixes
[AVFoundation's missing-sample defect](https://lists.ffmpeg.org/pipermail/ffmpeg-trac/2025-January/072278.html).
Stable FFmpeg 8.1.x still uses a single overwritten capture buffer; Echo Bridge
therefore uses `tools/voice-uplink/.ffmpeg/bin/ffmpeg` without replacing the
system installation. The fixed binary is ignored by Git and `run.sh` selects it
automatically.

Flash the firmware and leave its serial monitor visible. Once the peer is
connected, the harness starts capture, plays the deterministic phrase, writes
the received WAV, transcribes it locally, and sends the final transcript back
through the provider event path. It then sends a separate, fixed downlink
conformance program: a 660 Hz opening marker, a 300 ms gap, a locally
synthesized phrase, a 160 ms gap, and a 300 ms 880 Hz closing marker. The
default phrase is “Please set the timer for five minutes” repeated twice. The
repetition keeps the room-acoustic WER gate stable while still exposing
missing, reordered, or unintelligible speech. The two markers provide an
acoustic duration clock that does not depend on fragile room-noise thresholds.
A 100 ms silent RTP
warm-up primes the ESP receive jitter buffer before the measured program. No
hosted speech or AI credentials are used. Echo Bridge temporarily sets
`--speaker-volume` and restores the prior Mac output volume/mute state
afterward. Use `--runs 10` for the repeatability gate.

The uplink and downlink phrases are independent. Override either phrase and its
generated audio path explicitly:

```sh
./tools/voice-uplink/run.sh \
  --phrase 'Set the timer for five minutes.' \
  --phrase-audio tools/voice-uplink/artifacts/uplink-phrase.aiff \
  --downlink-phrase 'The quick brown fox jumps over the lazy dog.' \
  --downlink-phrase-audio tools/voice-uplink/artifacts/downlink-phrase.aiff
```

Every run writes packet-timing evidence to `playback-analysis.json`. Recording
what physically comes out of the CoreS3 speaker is explicitly opt-in:

```sh
./tools/voice-uplink/run.sh --runs 1 --capture-playback
```

The first capture may produce a macOS permission prompt. Approve the app that
launched Echo Bridge (Codex or Terminal) under **System Settings > Privacy &
Security > Microphone**, then rerun. AVFoundation's `:default` selector follows
the macOS default input. To list or override it:

```sh
ffmpeg -hide_banner -f avfoundation -list_devices true -i "" || true
./tools/voice-uplink/run.sh --runs 1 --capture-playback \
  --capture-device ':MacBook Pro Microphone'
```

Echo Bridge records the current macOS input-volume setting in
`playback-analysis.json` but never changes it. Automatic input gain proved less
reliable than the system's existing setting. If a capture is unusually quiet,
move the watch closer first; manual adjustment remains available when useful:

```sh
osascript -e 'set volume input volume 70'
```

Because that command is manual, restore your preferred input volume yourself
after the experiment.

For the complete 0/2/10-second idle-gap run, also let Echo Bridge own the USB
serial port so it can gate the firmware's final playback counters. Close any
other serial monitor first and substitute the device shown by
`ls /dev/cu.usb*`:

```sh
./tools/voice-uplink/run.sh \
  --runs 3 \
  --inter-run-gaps 0,2,10 \
  --capture-playback \
  --serial-port /dev/cu.usbmodem101
```

One `--inter-run-gaps` value is repeated for all runs; otherwise provide exactly
one value per run. Each value is the idle delay immediately before that run.

The physical gate requires both acoustic markers to be within 20 Hz of their
660 Hz and 880 Hz references, the opening-tone duration and marker-to-marker
program duration to be within 10% of their sources, downlink-phrase WER no
greater than 0.25, and no generated downlink packet interval below 10 ms. The
active-RMS span remains diagnostic only; it is not a duration gate. Captured
speech is isolated between the markers, adaptively denoised, and normalized
before local Whisper transcription. When
`--serial-port` is supplied, the final `downlink playback stopped` telemetry
must report Opus codec 3 and 16 kHz decoded PCM, while `dropped`, `underflow`,
and `speaker_fail` must all be zero.

Evidence is written under `tools/voice-uplink/artifacts/` and intentionally
ignored. Each run contains `watch-uplink.wav`, `speaker-downlink-source.wav`,
`result.json`, and `playback-analysis.json`. Physical capture additionally writes
`speaker-downlink-capture.wav`, an isolated `speaker-downlink-speech.wav`, local
Whisper evidence, and `ffmpeg-capture.log`. Serial capture writes both a complete
`artifacts/firmware-telemetry.log` and a per-run `firmware-telemetry.log`.
Without the optional capture flags, their gates are marked skipped while
transport and pacing are still tested.
