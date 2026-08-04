# Doodad live-agent service

This service is the Phase 0–4 foreground conversation and durable-control
vertical slice. It uses the firmware's Opus WebRTC seam, resamples
uplink/downlink audio at the process boundary, and runs a Pipecat cascade with
local Silero VAD and Smart Turn, streaming OpenAI transcription, a persistent
Responses WebSocket foreground model, and ElevenLabs Flash WebSocket TTS.

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
