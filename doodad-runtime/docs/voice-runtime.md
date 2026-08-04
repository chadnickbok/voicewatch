# Trusted voice runtime

Voice is a system capability, not an app-owned screen.

Persistent foreground conversation, typed watch actions, durable fake-worker
jobs, and attention policy are implemented. The next slice is the real Codex
app-builder worker described in the
[live foreground agent vertical-slice plan](live-agent-vertical-slice.md); see
the [current roadmap](roadmap.md) for overall sequencing.

## Layer and task ownership

The native host owns a voice overlay above the active route. A hardware gesture,
screen gesture, or allowed wake phrase can open it from any app. The active app
cannot cover, intercept, recolor, or replace that layer.

```text
microphone/I2S task
  -> bounded audio ring
  -> transport/session task
  -> partial transcript + structured action stream
  -> trusted UI queue
  -> VoiceOrb / transcript / clarification / review overlay
  -> capability router
       -> system navigation
       -> app semantic action mailbox
       -> package build/install pipeline
       -> typed shared-state transaction
```

Only the UI task mutates LVGL. Audio and network tasks post immutable messages.
Replaceable partial transcripts coalesce; user commits, cancellations,
permission decisions, and app actions are never silently dropped.

## Session state machine

```text
disconnected -> ready -- explicit press --> listening -> thinking
                  ^            |                |
                  |            v                v
                  +-------- cancelled       clarify/review
                  |                             |
                  |                             v
                  +--------- speaking <---- executing
                                |
                                +-- press --> listening

any active phase -- cancel --> ready
any transport failure -------> disconnected/error
```

Every transition has a timeout, cancellation path, reduced-motion visual, and
semantic announcement. A WebRTC connection alone never starts or reserves the
microphone. Capture begins only after an explicit trusted-system action and the
microphone is shut down before the overlay reports that listening has stopped.
Completing assistant playback returns to `ready`, not `listening`.

The runtime overlay is data-driven rather than a catalog fixture: the server's
bounded interim/final transcript and assistant response are carried in
`agent.state`, while local PCM peaks drive the listening ring without a network
round trip. AppSpec mounts and command batches are suppressed while the trusted
overlay owns the display, preventing an app from replacing or invalidating the
system voice surface.

## Action boundary

The server returns typed proposals, never native function names:

- `navigate.home`, `navigate.app`, `navigate.back`;
- `app.action` with app ID, stable ActionId, and typed payload;
- `app.generate` / `app.update` requests entering the trusted build pipeline;
- `state.transaction` against a permitted typed namespace;
- `system.preference` against an allowlist.

The host checks capability, target lifecycle, payload schema, and confirmation
policy. Destructive, sensitive, permission-expanding, or ambiguous proposals
enter `ChangeReview`, `PermissionReview`, or `ClarificationChoiceGroup`.

## Interruption and restoration

Opening voice snapshots the active route, focus target, scroll anchor, and
modal state. Closing restores them only if the underlying app generation is
unchanged. If an action switched or updated the app, the new route becomes the
restoration target. A crashed or hung guest cannot prevent voice cancellation,
Home navigation, permission review, or recovery.

The `apps/voice/appspec.json` fixture exercises the public semantic shape for
preview, but the production voice overlay remains host-owned native UI.

## Physical voice transport

The native `voice_service`, production live-agent, and local Echo Bridge harness
share the current bidirectional wideband transport:

```text
CoreS3 microphone -> Opus/WebRTC -> live-agent STT/model/tools/TTS
                 -> Opus/WebRTC -> CoreS3 speaker

Mac test phrase -> Mac speaker -> CoreS3 microphone -> Opus/WebRTC
                -> Echo Bridge WAV -> whisper.cpp -> transcript.final
                -> provider event -> voice-notes Wasm UI
```

WebSocket is limited to bounded, versioned signaling and control messages;
audio uses an Espressif PeerConnection with DTLS-SRTP. The Mac endpoint is
discovered over mDNS, with an optional static URL for hardware diagnostics.
The watch owns Wi-Fi, microphone access, codec selection, and the WebRTC
session. Wasm can request capture and receive lifecycle/transcript events but
never receives raw microphone buffers or network access.

Opus uses 48 kHz RTP with 16 kHz mono PCM at the application edges. The
production and Echo Bridge lanes exercise bidirectional pacing, bounded queues,
and the CoreS3 microphone/speaker handoff under the complete display, WAMR,
Wi-Fi, and TLS layout. A compile-time PCMU fallback remains an implementation
option, not the current conformance baseline.

Run `tools/voice-uplink/setup.sh`, flash a voice-notes firmware build, and run
`tools/voice-uplink/run.sh`. The harness temporarily makes the Mac output
audible, restores the previous volume/mute state, captures a WAV, reports sent
and received frame counts plus word error rate, returns the transcript to the
watch, and writes ignored evidence under `tools/voice-uplink/artifacts/`.
