# Trusted voice runtime

Voice is a system capability, not an app-owned screen.

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
idle -> opening -> listening -> thinking
                    |             |
                    v             v
                 cancelled     clarify/review
                                  |
                                  v
                         executing -> speaking -> closing
                                  |
                                  v
                                error
```

Every transition has a timeout, cancellation path, reduced-motion visual, and
semantic announcement. Microphone capture stops before the overlay reports
that listening has stopped.

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

## Physical microphone uplink slice

The first working transport slice is implemented by the native
`voice_service` and the local `tools/voice-uplink` Echo Bridge harness:

```text
Mac test phrase -> Mac speaker -> CoreS3 microphone -> PCMU/WebRTC
                -> Mac WAV -> whisper.cpp -> transcript.final
                -> provider event -> voice-notes Wasm UI
```

WebSocket is limited to bounded, versioned signaling and control messages;
audio uses an Espressif PeerConnection with DTLS-SRTP. The Mac endpoint is
discovered over mDNS, with an optional static URL for hardware diagnostics.
The watch owns Wi-Fi, microphone access, codec selection, and the WebRTC
session. Wasm can request capture and receive lifecycle/transcript events but
never receives raw microphone buffers or network access.

PCMU at 8 kHz is the baseline conformance codec because it sustains real-time
20 ms frames on ESP32-S3 with the complete display, WAMR, Wi-Fi, and TLS stack
resident. Opus remains a future bandwidth-optimized profile; it should only be
enabled after its encoder is moved off the capture path or shown to maintain
real-time pacing under the production memory layout.

Run `tools/voice-uplink/setup.sh`, flash a voice-notes firmware build, and run
`tools/voice-uplink/run.sh`. The harness temporarily makes the Mac output
audible, restores the previous volume/mute state, captures a WAV, reports sent
and received frame counts plus word error rate, returns the transcript to the
watch, and writes ignored evidence under `tools/voice-uplink/artifacts/`.
