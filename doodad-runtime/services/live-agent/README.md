# Doodad live-agent service

This service is the Phase 0–6 foreground conversation, durable-control, and
personal-app delivery vertical slice. It uses the firmware's Opus WebRTC seam,
resamples uplink/downlink audio at the process boundary, and runs a Pipecat
cascade with local Silero VAD and Smart Turn, streaming OpenAI transcription,
a persistent Responses WebSocket foreground model, and ElevenLabs Flash
WebSocket TTS.

The CoreS3 uses a shared microphone/speaker codec and an explicit push-to-talk
lifecycle. Connecting the watch leaves the microphone off and the active app or
watch face visible. Hold Button B to open the trusted Voice Orb and begin one
turn. While listening, the ring follows the microphone level and the transcript
replaces the prompt as it arrives. Tap the orb or Button B again to finish;
natural end-of-speech may finish first. Tap the close action or Button A to
cancel. After playback the overlay returns to **Ready** and does not silently
start another capture.

Touching the orb while the assistant is speaking clears queued speech,
interrupts the Pipecat turn, and begins a new capture. Simultaneous acoustic
barge-in is not supported by this board.

Long answers are retained in a server-side response journal and a lossless
per-utterance audio spool. ElevenLabs may synthesize faster than realtime, but
the WebRTC track paces every frame to the watch instead of dropping the tail.
Starting a new capture interrupts the Pipecat turn, closes the active TTS
context, and discards all unplayed audio and uncommitted assistant text.

Optional capacity overrides are:

- `DOODAD_MAX_COMPLETION_TOKENS` (default `4096`)
- `DOODAD_MAX_RESPONSE_TEXT_BYTES` (default `262144`)
- `DOODAD_DOWNLINK_MAX_SPOOL_SECONDS` (default `600`)

The watch still receives only a 160-character display projection. Capacity
violations are explicit service errors and telemetry events; ordinary long
responses are never silently truncated.

Production `start_app_build` jobs use the pinned Codex app-server binary over
supervised stdio. Each job writes only to its own Application Support workspace,
persists thread/turn/question/artifact state in SQLite, and reaches
`ready_for_review` only after the deterministic verifier and outer personal
packager succeed. Codex itself never receives a signing key, installs an app, or
touches hardware. The deterministic verifier owns schema, semantics,
permissions, Rust/Wasm, build/check/test, timer conformance, and the 240×240
simulator render. The outer packager then authenticates owner/app/version/ABI
and payload-hash metadata plus raw `app.wasm` as DDB1 with the local user's
HMAC key, outside the mutable Codex workspace.

With personal delivery configured, this same aiohttp service announces the
durable bundle as `app.ready` over the existing WebSocket and serves immutable
bytes from `/apps/<bundle_sha256>` over HTTP on the same port. The CoreS3 owns
download, verification, installation, **Launch now**, live guest switching,
and current/previous rollback. Published-app/store trust is later policy.

Set these values explicitly on the service and to the same values in firmware's
**Doodad personal apps** configuration:

- `DOODAD_PERSONAL_OWNER_ID`;
- `DOODAD_PERSONAL_SIGNER_KEY_ID` (default `personal-v1`); and
- `DOODAD_PERSONAL_HMAC_KEY_HEX` (exactly 32 bytes / 64 hex characters).

The immutable host store defaults to
`~/Library/Application Support/Doodad/personal-apps`; use
`DOODAD_PERSONAL_ARTIFACT_ROOT` to override it. See the
[personal-app setup and manual CoreS3 gate](../../docs/personal-app-installation.md).

Set
`DOODAD_CODEX_BINARY` or `DOODAD_CODEX_WORKSPACE_ROOT` only when overriding the
defaults. Packaged deployments outside the monorepo can set
`DOODAD_RUNTIME_ROOT`. Regenerate the checked protocol subset with:

```sh
./services/live-agent/scripts/generate-codex-protocol.sh
```

From the repository root:

```sh
./scripts/test-live-agent.sh
./scripts/run-live-agent.sh check-config
./scripts/run-live-agent.sh serve
```

`run-live-agent.sh` loads `../openai.env` and `../elevenlabs.env` without printing
their contents. Runtime state defaults to Application Support and latency events
to `~/Library/Logs/Doodad/live-agent-latency.jsonl`. Use `fake-demo --database
/tmp/doodad-demo.sqlite3` for a provider-free lifecycle demonstration.

The Phase 6 physical CoreS3 gate is still pending. To run it, start `serve`, say
“Build me a rest timer,” and ask an unrelated workout question while the build
badge remains active. At the next natural pause answer “ring.” After independent
verification, expect one `app.ready` trace, watch download/verification, and an
**APP READY** screen with **Launch now** and **Later**. Launch the timer, return
Home, reopen it from **APPS**, then validate a second-generation detectable
failure restores the prior version without rebooting. Record serial and screen
evidence using the linked manual procedure; this README does not claim that run
has already occurred.
