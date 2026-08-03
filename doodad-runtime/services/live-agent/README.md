# Doodad live-agent service

This service is the Phase 0–4 foreground conversation and durable-control
vertical slice. It retains the firmware's proven PCMU WebRTC seam, resamples
uplink/downlink audio at the process boundary, and runs a Pipecat cascade with
local Silero VAD and Smart Turn, streaming OpenAI transcription, a persistent
Responses WebSocket foreground model, and ElevenLabs Flash WebSocket TTS.

The CoreS3 uses a shared microphone/speaker codec. During assistant playback,
touching the Voice Orb starts capture, clears queued speech, and interrupts the
Pipecat turn. Simultaneous acoustic barge-in is not supported by this board.

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
