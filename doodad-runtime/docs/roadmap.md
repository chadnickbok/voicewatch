# Doodad roadmap

| Field | Value |
|---|---|
| Status | Current overall roadmap |
| Last reconciled | 2026-08-04 implementation audit |
| Current milestone | Personal-app install/live-launch CoreS3 qualification |

This is the only project-wide sequencing document. Detailed plans and
conformance trackers remain useful, but they do not independently set the
overall implementation order.

## Proven foundation

- The trusted ESP-IDF shell runs one bounded Rust/Wasm application through
  WAMR, live-switches that guest without rebooting, and retains native Home,
  Voice, and embedded/legacy recovery paths.
- AppSpec, CommandBatch, the native Material/LVGL renderer, the simulator, and
  the twenty-app conformance corpus exercise the UI and guest boundary.
- CoreS3 voice uses duplex Opus/WebRTC, push-to-talk, streaming STT/model/TTS,
  interruption, typed watch actions, and an orthogonal background-job UI.
- Durable fake and production Codex app-build jobs, focused questions, restart
  recovery, independent package verification, and attention delivery are
  implemented. T-Watch board/build support exists, but full physical
  qualification is still outstanding.
- The personal-app v0 code path packages verified output outside Codex, serves
  and announces immutable bundles, verifies and installs them into a multi-app
  current/previous registry, presents an installed-app launcher, and namespaces
  exact timers by app. Physical CoreS3 evidence for that complete loop has not
  yet been recorded.

These foundations are sufficiently validated to move product development from
transport and UI proof toward useful agent behavior. They are not a claim that
the personal-app loop has passed its physical gate, that shared backing data is
complete, or that published-app/store trust exists.

## Completed — Codex worker to `ready_for_review`

The production path now uses a supervised, pinned Codex app-server worker for
one bounded rest-timer brief. The fake worker remains the deterministic test
lane.

The completed path asks the durable ring/bar question, resumes the correct
Codex thread after service restart, generates outside the repository, runs ten
independent schema/build/Wasm/simulator/semantic/permission/timer gates, and
emits a compact artifact identity and hash as `ready_for_review`.

Personal packaging, watch transfer, installation, and launch were explicitly
outside this milestone and are the follow-on slice below. The Phase 5 execution
record is
[Phase 5 of the live-agent vertical slice](live-agent-vertical-slice.md#phase-5--codex-worker-and-rest-timer-generation).

## Now — qualify personal app install and live launch

Close the generated-app loop on physical CoreS3 using the deliberately small
personal trust profile. After independent verification, an outer packager
binds `owner_id`, `app_id`, semantic version, host ABI, and payload SHA-256 in
a canonical DDB1 envelope authenticated with the user's local HMAC key. The
live-agent announces `app.ready` over the existing WebSocket and serves the
bundle over HTTP on the same port. The watch verifies into temporary storage,
installs it, shows **Launch now**, and switches the single resident WAMR guest
without rebooting the native shell.

The implementation retains current and previous generations per app and
reloads the previous generation after a detectable startup or handler failure.
Scheduler records are app-owned, preventing a timer scheduled by one guest from
being delivered to another after a switch. `/packages/active.wasm` is only a
legacy fallback, not the registry's active pointer.

The decisive manual gate is: build a rest timer by voice, observe download and
verification followed by **APP READY**, tap **Launch now**, return Home and
reopen it from the installed-app launcher, then install a second generation and
prove a deliberately triggered guest failure restores the previous one without
a firmware reboot or loss of Home/Voice. Record logs and screen evidence; do
not treat desktop tests alone as physical completion. The exact procedure is in
[Personal app installation](personal-app-installation.md).

## Next — real backing-data query

Replace the mocked Workout storage path with one small real shared-data slice:
the Workout UI writes bounded durable completed-set records, and Voice reads a
typed workout-history summary from the same host-owned data.

The decisive manual flow is: log known sets through the Workout UI, ask a
question such as “How many squat sets did I complete today?”, reboot, and get
the same answer. Fixed fixtures or the current hard-coded “next set” response
do not satisfy this milestone.

## Later — hardening and hardware qualification

- a separate published-app trust profile with asymmetric publisher identity,
  review policy, revocation, capability grants, and store lifecycle if an app
  store becomes a real product requirement; personal apps remain explicitly
  tied to and trusted by their local user;
- scripted multi-turn evaluation, fault injection, latency/reliability reports,
  log redaction, and dependency-warning cleanup;
- physical T-Watch display, touch, haptic, microphone, speaker, reconnect,
  thermal, memory, power, and battery qualification;
- remaining Weather, Parallax, provider, persistence, accessibility, and power
  closure that is not required by the current product slice; and
- production security, update, migration, and recovery work beyond the personal
  v0 current/previous contract.

## Document roles

- [Live-agent vertical slice](live-agent-vertical-slice.md) — detailed phase
  plan and implementation record; Phases 0–6 are implemented in code, the
  Phase 6 physical CoreS3 gate is still open, and Phases 7–8 remain future.
- [Personal app installation](personal-app-installation.md) — DDB1/configuration
  contract and the open manual CoreS3 Phase 6 evidence procedure.
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
