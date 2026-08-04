# Doodad roadmap

| Field | Value |
|---|---|
| Status | Current overall roadmap |
| Last reconciled | 2026-08-04 implementation audit |
| Current milestone | Real backing-data query |

This is the only project-wide sequencing document. Detailed plans and
conformance trackers remain useful, but they do not independently set the
overall implementation order.

## Proven foundation

- The trusted ESP-IDF shell runs bounded Rust/Wasm applications through WAMR,
  loads onboard or microSD app images, and retains an embedded recovery path.
- AppSpec, CommandBatch, the native Material/LVGL renderer, the simulator, and
  the twenty-app conformance corpus exercise the UI and guest boundary.
- CoreS3 voice uses duplex Opus/WebRTC, push-to-talk, streaming STT/model/TTS,
  interruption, typed watch actions, and an orthogonal background-job UI.
- Durable fake and production Codex app-build jobs, focused questions, restart
  recovery, independent package verification, and attention delivery are
  implemented. T-Watch board/build support exists, but full physical
  qualification is still outstanding.

These foundations are sufficiently validated to move product development from
transport and UI proof toward useful agent behavior. They are not a claim that
production package activation, shared data, or hardware qualification is done.

## Completed — Codex worker to `ready_for_review`

The production path now uses a supervised, pinned Codex app-server worker for
one bounded rest-timer brief. The fake worker remains the deterministic test
lane.

The completed path asks the durable ring/bar question, resumes the correct
Codex thread after service restart, generates outside the repository, runs ten
independent schema/build/Wasm/simulator/semantic/permission/timer gates, and
emits a compact artifact identity and hash as `ready_for_review`.

Package signing, watch transfer, installation, and activation are explicitly
outside this milestone. The detailed execution plan is
[Phase 5 of the live-agent vertical slice](live-agent-vertical-slice.md#phase-5--codex-worker-and-rest-timer-generation).

## Now — real backing-data query

Replace the mocked Workout storage path with one small real shared-data slice:
the Workout UI writes bounded durable completed-set records, and Voice reads a
typed workout-history summary from the same host-owned data.

The decisive manual flow is: log known sets through the Workout UI, ask a
question such as “How many squat sets did I complete today?”, reboot, and get
the same answer. Fixed fixtures or the current hard-coded “next set” response
do not satisfy this milestone.

## Then — reviewed package activation

Define the canonical bundle and development signing flow, transfer a generated
artifact to inactive storage, perform trusted permission/artifact review,
activate atomically, confirm launch health, and roll back a deliberately bad
generation without losing Home, Voice, or recovery.

This is [Phase 6 of the live-agent vertical slice](live-agent-vertical-slice.md#phase-6--signed-package-staging-and-cores3-activation).

## Later — hardening and hardware qualification

- scripted multi-turn evaluation, fault injection, latency/reliability reports,
  log redaction, and dependency-warning cleanup;
- physical T-Watch display, touch, haptic, microphone, speaker, reconnect,
  thermal, memory, power, and battery qualification;
- remaining Weather, Parallax, provider, persistence, accessibility, and power
  closure that is not required by the current product slice; and
- production security, update, migration, and recovery work.

## Document roles

- [Live-agent vertical slice](live-agent-vertical-slice.md) — detailed phase
  plan; Phases 0–5 are implemented and Phase 6 remains gated behind the current
  backing-data slice.
- [Project Parallax](project-parallax.md) — dedicated dual-renderer conformance
  tracker, not the overall product roadmap.
- [20-app conformance suite](20-app-conformance-suite.md) — permanent UI/runtime
  executable specification with production providers intentionally separate.
- [Project Nimbus](project-nimbus-weather-plan.md) — implemented Weather project
  record with focused hardware closure still open.
- [Material 3 Expressive implementation specification](material3-expressive-lvgl-implementation-plan.md)
  — reference specification and historical phased plan, not a current tracker.
- [First vertical-slice plan](../../PLAN.md) — completed historical milestone
  brief.

## Maintenance rule

Every active project-level planning document should identify its role, status,
last reconciliation date, and next objective gate. Completed work should link
to durable tests or evidence. When overall ordering changes, update this file
and link the focused plan rather than adding another independent master
roadmap.
