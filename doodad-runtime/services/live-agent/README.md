# Doodad live-agent service

This service is the Phase 0–6 foreground conversation, durable-control, and
personal-app delivery vertical slice. It uses the firmware's Opus WebRTC seam,
resamples uplink/downlink audio at the process boundary, and runs a Pipecat
cascade with local Silero VAD and Smart Turn, streaming OpenAI transcription,
a persistent Responses WebSocket foreground model, and ElevenLabs Flash
WebSocket TTS.

The MoQ migration now has an explicit `serve --transport moq --moq-config ...`
mode connecting the existing conversation callbacks to authenticated WSS and
the native Rust worker over private IPC. It is a development mode: authenticated
full-shell Ultra startup, audio and response replacement now pass a private
hardware bench. A host-initiated physical microphone/STT/tool/TTS turn and fresh
session reconnect also pass; physical PTT interaction and release acceptance
remain open.
WebRTC remains the default and deployed mode. See the
[MoQ product session contract](../../../docs/moq-product-session.md) for private
configuration, tests and limitations. `aiortc` is an optional `webrtc` extra; install with
`uv sync --extra webrtc` for the existing service. Development tests also include
that dependency to preserve legacy coverage.

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

`DOODAD_STT_NOISE_REDUCTION` selects `off`, `near_field`, or `far_field`.
MoQ explicit push-to-talk defaults to `off`: the Ultra acoustic fixture was
misrecognized with `near_field` but recognized correctly without provider noise
filtering. The existing WebRTC path retains `near_field`. This does not change
the sample rate, microphone authorization or validated capture-end requirement.

MoQ additionally correlates STT results with acknowledged provider item IDs and
the originating capture. Cancelled captures cannot feed delayed transcripts to
the next turn. `tools/moq_provider_bench.py --cancel-first-stt` exercises this by
delaying one real provider final in memory, cancelling, and releasing it during
the replacement capture. It never injects or saves a transcript. Cancellation
after model/tool work starts remains a separate acceptance gate.

The watch still receives only a 160-character display projection. Capacity
violations are explicit service errors and telemetry events; ordinary long
responses are never silently truncated.

Production `start_app_build` jobs use the pinned Codex app-server binary over
supervised stdio. Each job writes only to its own Application Support workspace,
persists thread/turn/question/artifact state in SQLite, and reaches
`ready_for_review` only after the deterministic verifier and outer personal
packager succeed. The worker first runs a real Codex plan-mode turn, routes up
to three necessary clarification questions through the watch's focused voice
path, and records explicit voice approval against the plan hash. A separate
ImageGen turn creates one to three 240×240 visual targets from the checked-in
Doodad design language before implementation begins.

Codex itself never receives a signing key, installs an app, or touches
hardware. The deterministic verifier owns schema, semantics, plan/manifest
agreement, permissions, Rust/Wasm, build/check/test, capability-specific
conformance, simulator rendering, and the primary target-versus-simulator
comparison. `DOODAD_VISUAL_MAX_RMSE` can override the bounded structural-RMSE
threshold (default `0.38`) for calibration runs. The outer packager then authenticates owner/app/version/ABI
signed semantic icon/theme identity and payload-hash metadata plus raw `app.wasm` as DDB1 with the local user's
HMAC key, outside the mutable Codex workspace.

The same durable ledger now tracks app builds, research reports, and slide-deck
delivery as independent concurrent tasks. Every `agent.state` projection carries
up to three bounded task rows with real job IDs, stages, progress, and elapsed
time; firmware uses those rows directly for the Agents list and detail screen.
The bounded wire shape is `contracts/agent-state-v1.schema.json` and remains
below the firmware's 16 KiB signaling limit.
The foreground model has typed tools to start general work and read current task
status, so status answers come from SQLite rather than conversational memory.

Research work produces a Markdown report. Presentation work asks Codex for a
bounded Markdown outline, converts it host-side into a real `.pptx`, and keeps
email credentials outside the Codex process. Configure delivery with
`DOODAD_SMTP_HOST`, `DOODAD_SMTP_SENDER`, and optionally `DOODAD_SMTP_PORT`,
`DOODAD_SMTP_USERNAME`, and `DOODAD_SMTP_PASSWORD`. Without SMTP configuration,
the task fails visibly at the controlled delivery gate instead of pretending it
sent mail.

For microphone-free live rehearsals, an identified signaling client may send a
v1 `conversation.text` payload containing `{"text":"..."}`. It enters the normal
foreground model/tool/TTS path after the microphone and STT boundary; production
watch interaction remains push-to-talk.

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
`DOODAD_RUNTIME_ROOT`. `DOODAD_GENERATED_CAPABILITIES` is an optional
comma-separated target allowlist; its safe default is UI plus host-owned timer
capabilities. Regenerate the checked protocol subset with:

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
their contents. On macOS it also loads the personal HMAC key from Keychain
service `voicewatch.doodad.personal.hmac`, using the configured owner or
`local.nick`, when no explicit key is present. Runtime state defaults to Application Support and latency events
to `~/Library/Logs/Doodad/live-agent-latency.jsonl`. Use `fake-demo --database
/tmp/doodad-demo.sqlite3` for a provider-free lifecycle demonstration.

For a pickup-ready Mac service that restarts after failure and launches at
login, install the user LaunchAgent once:

```sh
./scripts/live-agent-service.sh install
./scripts/live-agent-service.sh status
```

The installer deploys a runtime snapshot and mode-600 provider configuration
under `~/Library/Application Support/Doodad`. macOS background services cannot
reliably read a source checkout in `Documents`; rerun `install` after changing
the service or generated-app runtime.

The Phase 6 physical CoreS3 gate is still pending. To run it, start `serve`, say
“Build me a rest timer,” and ask an unrelated workout question while the build
badge remains active. At the next natural pause answer “ring,” review the spoken
plan, then say “approve.” After design generation and independent
verification, expect one `app.ready` trace, watch download/verification, and an
**APP READY** screen with **Launch now** and **Later**. Launch the timer, return
Home, reopen it from **APPS**, then validate a second-generation detectable
failure restores the prior version without rebooting. Record serial and screen
evidence using the linked manual procedure; this README does not claim that run
has already occurred.
