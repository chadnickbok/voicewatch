# Doodad live-agent service

The MoQ host now selects the reviewed terminal-reader fix through the library's
pinned, hash-verified vendored Rust dependency. Wire formats and the independent
unchanged reference peer/oracle are preserved. The provider bench's explicit
`--session-seconds 600 --capture-rounds 3` checks three spaced ordinary turns,
monitored idle, credential renewal and final reconnect. It rejects induced
network impairment and playout stalls in that mode. This is a ten-minute session
check including idle, not ten minutes of continuous speech or physical buttons.

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
WebRTC remains the default and its existing deployment is preserved. An explicit
paired Mac MoQ service is now available through `live-agent-service.sh --moq`;
see [supervision and local deployment](../../../docs/moq-supervised-host.md).
See the
[MoQ product session contract](../../../docs/moq-product-session.md) for private
configuration, tests and limitations. `aiortc` is an optional `webrtc` extra; install with
`uv sync --extra webrtc` for the existing service. Development tests also include
that dependency to preserve legacy coverage.

Service startup now owns listener cleanup even when mDNS registration fails.
Discovery uses the asynchronous Zeroconf API and closes its resources on a
registration error or cancellation. `serve --no-discovery` disables advertisement
for clients with explicit endpoints; the private provider bench uses it to avoid
colliding with a running permanent service. Normal deployments still advertise
by default. The bench requires `service.ready`, a live service owner throughout
the run, and `shutdown.completed`; a listening socket alone is insufficient.

The private provider bench supports `--packet-reorder-ms 80` (also 40 or 250)
and `--packet-duplicate-every 7` (also 16). Reordering adds that delay to every
eighth received UDP datagram that survives loss. Duplication resends each selected
datagram unchanged five milliseconds later. Both directions must record actual
out-of-order delivery or duplication, respectively; merely scheduling a fault
does not pass. Originals and copies share the existing 256-packet pending bound,
and scheduled replies retain their original destination across a client-port
change. These are QUIC packet tests, not substitutes for application-group
ordering or a deliberately flow-control-blocked stream. No ambient audio is
persisted, and the speech policy remains unchanged.

`--playout-stall-ms 350` pauses only the first response's media pump after eight
packets. It must trigger the unchanged 200 ms pacing-debt limit, cancel that
response without committing unheard speech, and return the conversation to
Ready. Fresh provider turns must then complete on the same authenticated session.
The sink fences failed output, ends bot-speaking state and interrupts the failed
provider turn; a late failed drain cannot reset a replacement turn. Undelivered
durable announcements remain pending. This recovery case does not count the
deliberately cancelled response as successfully played.

`--group-delay-ms 250 --endpoint /tmp/moq-group-delay-host/debug/voicewatch-moq-endpoint`
instead holds one standard Hang audio group while later groups continue. Build
that separate endpoint with the library's `group-delay-fixture` feature; normal
binaries reject the diagnostic configuration. The bench requires one observed
hold/release and fresh groups before release, plus its unchanged provider,
speech, exact playback and reconnect checks. This fault runs separately from
cancellation fixtures. It does not change the permanent service or firmware,
and permanent enrollment must be reapplied afterward. It is an application-group
delay, not a deliberate QUIC byte-credit block or a calibrated latency test.

Firmware advertising `moq_renewal_v1` can renew an active session at half its
authorization lease. The host sends one bounded nonce; the watch proves its
enrollment key in a session-bound signing domain. The host extends only that
live grant, waits for native IPC acknowledgment, then returns a signed fresh-time
proof and bounded expiry. The watch verifies those before updating the library
and control deadlines and acknowledging completion. Replay, timeout, changed
ownership and expired leases fail closed. No identity, scope, media generation,
UTC value or microphone state changes during renewal. An older firmware session
still expires and reconnects. Deploy the matching native binary with the Python
host; this work does not update an already-running supervised service.

`tools/moq_ultra_bench.py --long-response-seconds 600` exercises generated paced
output with a non-frame-aligned tail across repeated short leases. It leaves the
microphone closed unless `--audio` is explicitly supplied, requires one continuous
session and the exact speaker receipt, and checks player PLC, late-packet,
queue-pressure and fallback-silence counters. These are not acoustic or DMA
underrun measurements. It also requires the final endpoint timing/heap snapshot
and records bounded host pacing diagnostics, without retaining PCM.
This is a synthetic long-playback gate, not a 600-second provider speech or full
product acceptance claim. The bench temporarily changes enrollment; reapply the
permanent enrollment afterward, without restoring firmware.

MoQ response pacing preserves the contiguous sample clock across scheduler
slips. It recovers at no more than one packet per 10 ms (twice nominal rate),
rather than shifting every later deadline. Falling more than 200 ms behind
cancels the current response through normal media cancellation while preserving
its authenticated session. Each new response gets a fresh clock after media
readiness; a retired pacing await cannot move that replacement clock. WebRTC
retains its existing wall-time reanchoring behavior. This is a local source
change, not an update to either running supervised host.

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
the replacement capture. It never injects or saves a transcript.

MoQ also preserves capture ownership through model context, tool callbacks and
TTS contexts. The control writer rejects cancelled queued actions, and the sink
checks ownership again after device waits before delivering audio or committing
played history. TTS word alignment is isolated per context. The provider bench
adds mutually exclusive `--cancel-first-tool` and `--cancel-first-tts` cases that
hold real provider output and release it after cancellation during a replacement
capture. Successful fresh turns require tool completion, speaker receipts and a
new played assistant-history message. No firmware restoration is required.

Text and idle background speech obtain a watch-issued output context without
microphone capture or an STT commit. `--text-first --background-first` exercises
both before the provider bench's microphone turns. `--background-first
--cancel-first-background` holds real TTS output, cancels it, and requires the
production idle loop to retry the still-pending test announcement. Only the
isolated bench database receives that test job; no external worker is launched.
History and durable attention delivery wait for matching speaker completion.

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
