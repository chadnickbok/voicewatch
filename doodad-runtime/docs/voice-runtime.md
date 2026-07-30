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
