# Doodad Echo Bridge

Echo Bridge is the local physical-device conformance harness for the first
voice transport slice:

```text
Mac `say` -> Mac speakers -> CoreS3 microphone -> PCMU/WebRTC -> Mac WAV
          -> whisper.cpp -> transcript.final -> voice-notes Wasm
```

The WebSocket carries only bounded, versioned signaling and control envelopes.
Audio is 8 kHz mono G.711 mu-law (PCMU) in a send-only WebRTC track. The Mac
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

Flash the firmware and leave its serial monitor visible. Once the peer is
connected, the harness starts capture, plays the deterministic phrase, writes
the received WAV, transcribes it locally, and sends the final transcript back
through the provider event path. It temporarily sets `--speaker-volume` and
restores the prior Mac volume/mute state afterward. Use `--runs 10` for the
repeatability gate.

Evidence is written under `tools/voice-uplink/artifacts/` and intentionally
ignored. A run passes the transport gate only when it receives encoded frames;
WER is reported separately because speaker placement and room acoustics are
physical test variables.
