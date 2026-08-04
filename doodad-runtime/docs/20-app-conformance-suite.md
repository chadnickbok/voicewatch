# Doodad 20-app and OS conformance suite

Document role: permanent UI/runtime conformance specification and completed
desktop-first execution plan; not the overall product roadmap

Status: desktop-first UI conformance slice implemented; production providers and
power-loss persistence remain separate networking/hardware milestones

Target: 240×240 square simulator and CoreS3 SE

Purpose: continuously stress and sanity-check the UI/runtime layer independently
of production provider and package-delivery work

Current product sequencing lives in the [Doodad roadmap](roadmap.md).

## 1. Outcome

Build a permanent conformance suite containing:

- 20 interactive watch applications;
- the Home/watch-face plane;
- the trusted Voice plane;
- App Manager and system control surfaces;
- deterministic provider simulations for every external dependency;
- full-app, glance, complication, notification, ongoing-activity, and
  voice-action projections where each domain requires them;
- golden images, semantic snapshots, scripted interactions, lifecycle tests,
  and resource reports.

This is not the start of a general-purpose app store. It is the executable
specification of the Doodad UI, package, state, lifecycle, and capability
boundaries.

The suite succeeds when difficult states are coherent across every surface,
not merely when 20 launch screens render.

Each app is a separate package fixture sharing the SDK, provider contracts,
and scenario library. The simulator OS launcher discovers those packages; the
suite is not compiled into one showcase guest.

## 2. Scope boundary

### In scope

- complete local interaction flows where the behavior is deterministic;
- realistic multi-screen app navigation;
- local state and transactional edits;
- mocked typed capability providers;
- simulated time, sleep, reboot, disconnect, stale data, errors, and recovery;
- host-owned Home, Voice, notification, ongoing, permission, and recovery UI;
- generated/downloadable Wasm apps where that is the intended architecture;
- trusted native apps where raw capabilities or sensitive data require them;
- hybrid apps whose presentation is replaceable but whose service is native;
- simulator goldens and CoreS3 performance traces.

### Out of scope for this pass

- production HTTP/WebSocket services;
- real phone synchronization;
- real weather, transit, sports, or smart-home credentials;
- production BLE protocols;
- production audio transcription;
- real GPS, heart-rate, or NFC support;
- signing, remote download, and production package activation;
- claims about all-night battery life before the T-Watch hardware lane exists.

External behavior is mocked at a typed provider boundary. App behavior above
that boundary is not mocked.

## 3. What “full app” means in this phase

Every app is an interactive conformance fixture with:

1. a full-app route and all core screens;
2. deterministic seeded data;
3. meaningful state changes and navigation;
4. success, empty, stale, offline, denied, loading, and error states when
   relevant;
5. every declared system-surface projection;
6. scripted semantic actions;
7. lifecycle scenarios such as background, restore, reboot, and provider
   reconnection;
8. visual, semantic, interaction, and resource evidence.

There are three implementation modes.

| Mode | Meaning | Apps |
|---|---|---|
| Downloadable | Rust/Wasm behavior plus AppSpec presentation | Tasks, Calculator, Workout, Calories, Medication/Habits, Transit, Smart Home, Sports, Microgame |
| Trusted | Native first-party capability owner with native or semantic presentation | Notifications, Voice Notes, Sensor Recorder, Sleep, Pass Wallet, Remote Control Lab |
| Hybrid | Native service/session plus replaceable AppSpec or native semantic presentation | Timer, Weather, Calendar, Media, Navigation |

Mode is a trust and lifecycle decision, not a visual-quality tier. All three
use the same Material framework and semantic-tree rules.

## 4. Current baseline

The repository already provides:

- a fixed 240×240 RGB565 simulator and centered CoreS3 viewport;
- a broad native Material 3 Expressive LVGL component framework;
- canonical JSON-to-CBOR AppSpec v1 validation;
- a narrow generated-app vocabulary;
- a resident WAMR guest with semantic events and atomic CommandBatch updates;
- typed native state with transactional writes and bindings;
- a native route stack and trusted overlay ownership model;
- deterministic catalog goldens;
- Hello, Calories, Calculator, Workout, and Voice fixtures;
- a complete desktop-to-firmware regression command.

All 20 entries now exist as separate, buildable Rust/Wasm conformance
packages. Every package has a manifest, canonical AppSpec bytes, a real
semantic interaction path through WAMR, every declared surface projection,
and deterministic scenarios. The apps are not screenshot fixtures or
one-button placeholders.

Timer, Weather, Notifications, Tasks, Calculator, Calories, Workout, and Snake
have bespoke state machines. The remaining packages execute multi-screen core,
failure/offline, and recovery flows through domain-scoped typed provider
imports. Calendar, audio, medication, sensors, sleep, media, navigation,
transit, home, sports, wallet, remote control, workout storage, and game-clock
requests are distinct Wasm capabilities; no suite app uses the diagnostic
`fixture.interact` capability.

The trusted main app now owns tested Home/Live Cards/launcher/control-center,
Voice overlay, and App Manager state machines. Desktop renders exist for all
six shell states, and the CoreS3 firmware maps its lower capacitive A/B/C strip
and PMIC power button onto the same semantic system intents.

The first Wave 1 vertical paths are now functional rather than mock
acknowledgements:

- Calculator carries the exact keypad key in its semantic event, runs
  fixed-point arithmetic in Wasm, and updates the result node without a
  remount.
- Timer carries stepper values, calls a capability-scoped native exact
  scheduler, receives versioned provider events, and remains interactive
  through start, countdown, cancellation, firing, and dismissal.
- The scheduler uses durable scenario deadlines, fixed-capacity journal
  snapshots, and one-shot firing transitions tested across background, display
  sleep, civil-time changes, and simulated reboot.
- The trusted surface registry atomically accepts one domain revision across
  app, glance, complication, notification, ongoing, and Voice projections,
  then drives shell counts. Timer publishes through this path in firmware.
- Desktop and firmware share the same provider-event encoder, AppSpec renderer,
  Wasm SDK decoder, and host capability signatures.
- Calendar, Voice Notes, Medication, Sensor Recorder, Sleep, Media, Navigation,
  Transit, Smart Home, Sports, Wallet, and Remote Control each traverse four
  bounded screens through the host provider boundary. Their flows exercise
  offline buffering, time-zone presentation, reminder responses, recording
  lifecycle, low-power sleep presentation, optimistic reconciliation, cached
  route/transit data, rollback and trusted review, burst coalescing, signed
  update rejection, and idempotent acknowledgement.
- Calories performs cumulative quick-add edits, progress-range mutation, and a
  structured voice review without rebuilding the Home screen. Workout carries
  stepper state through set commit, rest, next set, and summary. Snake is a
  deterministic playable 8×8 Wasm game with movement, food, growth, collision,
  reset, and quit.
- Every app has baseline, stale, and recovered surface snapshots. A lifecycle
  scenario publishes revisions 1→2→3 and verifies that every declared
  projection changes atomically at the same revision.
- Every decisive flow records a checked-in semantic tree and RGB565 hash at
  every stage, plus changed pixels, mounted nodes/events, LVGL object count and
  depth, guest module/AppSpec size, event count, and provider-request count.
- A two-cycle stress test replaces the resident guest across all 20 packages,
  executes every decisive flow, and asserts bounded objects and at least 18
  distinct final frames. Display-sleep and guest-replacement tests prove the
  exact timer service continues independently of rendering and resumes once.
- The trusted shell now renders distinct app-detail, install-progress,
  quarantine/rollback, notification, permission-review, hazardous-action,
  provider-error, and Voice phase states. Quarantined packages cannot publish
  Home, notification, or ongoing projections.

The following are deliberately deferred beyond this desktop UI slice:

- collection/list binding and keyed item updates;
- canonical lowering and renderer integration for the existing typed binding
  and Store foundations;
- bounded structural insert/remove/reorder transactions;
- NVS-backed scheduler journal persistence on hardware;
- real notification/source synchronization and production ongoing services;
- real audio/sensor/network streams and protocol-specific retry behavior;
- long-duration audio, sensor, thermal, battery, and power budgets;
- signing, download, and atomic package activation.

The CoreS3 trace now proves the 16MB layout on the connected CoreS3 SE:
dual 3MiB firmware slots, a mounted 10,060KiB package filesystem, 8MiB Quad
PSRAM with a passing boot memory test, embedded recovery fallback without an
SD card, successful WAMR instantiation, AppSpec mounting, and steady state.
The current firmware leaves about 70% free in either firmware slot.

## 5. Shared conformance architecture

```text
scenario fixture
  ├── deterministic clock and lifecycle
  ├── seeded app/shared/system state
  └── scripted provider events
          |
          v
typed mock providers
  clock / scheduler / weather / location / phone / notification
  audio / sensor / media / navigation / transit / home / sports
  wallet / remote-action / storage
          |
          v
trusted service or serialized app actor
          |
          v
domain state with revision + observed-at + freshness
          |
          ├── full app
          ├── Home Live Card
          ├── complication
          ├── notification
          ├── ongoing activity
          └── voice action/review
          |
          v
native Material components + semantic tree + RGB565 framebuffer
```

### Provider rules

Every provider must:

- expose typed requests and events rather than arbitrary JSON callbacks;
- carry a monotonically increasing revision;
- distinguish observed time from effective domain time;
- represent loading, unavailable, permission-denied, stale, and error states;
- support deterministic delay, timeout, disconnect, reconnect, and malformed
  response scenarios;
- be replaceable by a real implementation without changing app UI logic;
- never call LVGL directly.

### One revision across surfaces

Full app, Home, complication, notification, ongoing activity, and Voice must be
projections of one authoritative domain revision. A fixture fails if two
surfaces silently show conflicting revisions.

### Deterministic lifecycle

The simulator must control:

- monotonic time;
- wall-clock time and time zone;
- display sleep/wake;
- app foreground/background;
- guest crash/hang;
- host service restart;
- simulated reboot;
- connectivity and provider availability;
- theme and reduced-motion changes.

No conformance test may depend on the developer’s real clock or network.

## 6. OS-owned screen suite

Downloaded apps cannot replace or cover these screens.

### 6.1 Home, watch face, and Live Card deck

Build these states:

1. watch face with time, date, battery, connectivity, and four bounded
   complications;
2. Live Card deck with current, stale, loading, empty, and failed cards;
3. expanded Live Card with one primary action;
4. app launcher in list and compact-grid adaptations;
5. ongoing-activity handoff for timer, workout, recording, navigation, media,
   and live sports;
6. notification peek and privacy-redacted peek;
7. contextual card ordering and stale-card quarantine;
8. theme/reduced-motion preview.
9. safe mode with a quarantined app and recovery launcher.

Acceptance:

- all cards are host-rendered from bounded Glance models;
- a broken guest cannot break Home;
- the same domain revision appears in Home and its app;
- card insertion/removal preserves the focal anchor;
- idle Home produces no stationary redraws;
- the launcher remains usable with all 20 apps installed.

The proposed gesture contract for scripted tests is:

- watch face is the immutable root;
- swipe up opens Live Cards;
- swipe down opens control center;
- center tap opens the launcher;
- swipe right navigates back;
- a global long-press or hardware gesture opens Voice.

### 6.2 Trusted Voice plane

Build these states:

1. opening and listening;
2. partial and final transcript;
3. thinking/offline/retrying;
4. clarification choices;
5. structured change review;
6. destructive-action confirmation;
7. permission review;
8. app generation/build/download/install progress;
9. success, warning, cancellation, and error;
10. interruption over every app category and restoration afterward.

Acceptance:

- Voice can open and cancel over any route;
- it snapshots and restores route, focus, scroll anchor, and modal state;
- sensitive or destructive proposals always use trusted review UI;
- a hung guest cannot block Voice, Home, or cancellation;
- transcript replacement does not rebuild the entire overlay.

### 6.3 App Manager and control center

Build these states:

1. installed-app list with version, storage, health, and update state;
2. app detail and declared surfaces;
3. human-readable capability and shared-data permissions;
4. install/update progress with an atomic activation boundary;
5. rollback and last-known-good selection;
6. crash, quarantine, and recovery;
7. storage pressure and cleanup;
8. battery, charging, connectivity, audio, and provider diagnostics;
9. theme editor and per-app theme precedence;
10. developer telemetry for heap, objects, dirty area, frames, events, and
    guest traps.

Acceptance:

- system actions remain available with every guest stopped;
- failed install/update never removes the last-known-good app;
- permission changes are explicit and auditable;
- quarantined apps cannot publish Home or ongoing surfaces;
- telemetry can identify the app and screen responsible for a regression.

Install, update, signing, rollback, and health-check states in this phase are
explicitly fixture-driven state-machine simulations. Their UI must not be
presented as evidence that the production package installer already exists.

## 7. The 20 application fixtures

Surface notation:

- **A** full app
- **G** Home glance/Live Card
- **C** complication
- **N** notification
- **O** ongoing activity
- **V** voice actions

### 1. Timer Suite — Hybrid — A G C N O V

Core screens:

- timer list with multiple concurrent timers;
- duration editor and presets;
- active timer;
- stopwatch and laps;
- alarms and recurrence;
- Pomodoro session;
- firing/snooze/dismiss screen.

Mock services: deterministic clock, exact scheduler, wake, haptic, speaker.

Decisive scenario: close the app, sleep the display, change wall-clock time,
simulate reboot, and verify that the monotonic timer fires once at the correct
instant on every surface.

### 2. Weather and Rain Nowcast — Hybrid — A G C N V

Core screens:

- current conditions;
- hourly forecast;
- precipitation timeline;
- daily forecast;
- location/favorite selection;
- severe alert;
- stale/offline/error states.

Mock services: location, forecast, nowcast, alert stream, cache.

Decisive scenario: move through fresh, stale, disconnected, and recovering
states while every surface reports the same data revision and freshness.

### 3. Notification Inbox and Quick Reply — Trusted — A G N V

Core screens:

- grouped inbox;
- detail with long Unicode content;
- action/reply sheet;
- privacy-redacted state;
- source-disconnected and synchronization-conflict states.

Mock services: phone/server bridge, notification ingress, reply acknowledgement.

Decisive scenario: receive while asleep, read, dismiss or reply, reconnect the
source, and prove the action is applied exactly once.

### 4. Tasks, Shopping Lists, and Reminders — Downloadable — A G C N V

Core screens:

- list collection;
- list detail;
- add/edit item;
- completed/undo state;
- reminder editor;
- conflict review.

Mock services: shared data, reminder scheduler, remote sync/conflict provider.

Decisive scenario: add milk and a 5 PM reminder while offline, then reconcile a
conflicting remote edit without losing either intent.

### 5. Calculator and Unit/Currency Converter — Downloadable — A V

Core screens:

- calculator keypad;
- expression/result;
- history;
- unit conversion;
- currency conversion with stale-rate state.

Mock services: deterministic currency-rate provider only.

Decisive scenario: sustain ten taps per second with no lost input while result
updates preserve keypad object identity.

### 6. Calendar and Agenda — Hybrid — A G C N V

Core screens:

- next event;
- day agenda;
- event detail;
- RSVP/action state;
- reminder;
- time-zone/DST conflict view.

Mock services: calendar provider, time zone, recurrence expander, sync.

Decisive scenario: move the simulated user across time zones and DST while
offline, edit the next event, reconnect, and keep every surface consistent.

### 7. Weight Workout Tracker — Downloadable — A G C N O V

Core screens:

- routine selection;
- active exercise/set;
- weight and reps steppers;
- rest timer;
- session summary;
- history.

Mock services: monotonic session timer, haptic, shared workout store.

Decisive scenario: completing a set atomically records it and starts rest; the
session survives sleep, navigation, guest crash, and simulated reboot.

### 8. Calories, Macros, and Hydration — Downloadable — A G C N V

Core screens:

- daily totals;
- macro breakdown;
- meal/history list;
- quick add;
- structured voice review;
- correction/delete/undo;
- hydration.

Mock services: shared nutrition store, optional food lookup.

Decisive scenario: a reviewed voice record updates all bound surfaces in one
revision without remounting the screen.

### 9. Voice Notes and Transcription — Trusted — A G N O V

Core screens:

- recording;
- level/waveform;
- paused/offline buffered state;
- streaming transcript;
- note list/detail;
- resumable upload.

Mock services: microphone, audio buffer, upload, transcription stream.

Decisive scenario: record through a network outage, resume upload without
duplicating chunks, and produce one durable searchable transcript.

### 10. Medication and Habit Reminders — Downloadable — A G C N V

Core screens:

- today schedule;
- item detail;
- create/edit recurrence;
- due reminder;
- taken/skip/snooze;
- adherence history.

Mock services: scheduler, audit journal, shared health store.

Decisive scenario: replace the app and simulate reboot around a due reminder;
deliver once and record each response once.

### 11. Activity and Sensor Recorder — Trusted — A G C N O V

Core screens:

- provider selection;
- live recording metrics;
- compact chart;
- pause/resume;
- session detail;
- export status.

Mock services: accelerometer, BLE heart-rate substitute, time-series store,
export.

Decisive scenario: replay a long high-rate fixture without blocking UI/Voice,
dropping committed samples, or growing retained UI state.

### 12. Sleep Tracker and Smart Alarm — Trusted — A G C N O V

Core screens:

- sleep setup;
- overnight ongoing surface;
- smart-alarm window;
- morning summary;
- sleep-stage timeline;
- history.

Mock services: low-rate accelerometer, classifier, scheduler, haptic.

Decisive scenario: replay eight simulated hours with the UI inactive, remain
within the measured service budget, and wake once inside the configured window.

### 13. Media Remote and Now Playing — Hybrid — A G C N O V

Core screens:

- now playing;
- playback controls;
- progress/seek;
- queue;
- output/volume;
- disconnected/reconciling state.

Mock services: phone/BLE media session, artwork cache, command acknowledgements.

Decisive scenario: issue optimistic play/pause/seek commands through a
disconnect and reconcile to authoritative state without double application.
The initial implementation must also load deterministic checked-in album art
through the content-addressed package asset path so Compose and LVGL validate
the same decode, crop, fallback, memory, and RGB565 behavior.

### 14. Navigation, Compass, and Breadcrumbs — Hybrid — A G C N O V

Core screens:

- compass;
- route overview;
- next maneuver;
- breadcrumb trail;
- route progress;
- provider-loss/recovery.

Mock services: phone/simulated location, compass, route cache, direction haptic.

Decisive scenario: continue meaningfully from cached route data during location
loss and resume without jumping backward or losing traveled progress.

### 15. Transit Departures — Downloadable — A G C N V

Core screens:

- favorite stops;
- departure list;
- route detail;
- service alert;
- stale cache and refresh.

Mock services: location, favorites, departure/alert provider.

Decisive scenario: render favorite departures immediately from cache, show
their age, refresh asynchronously, and preserve the selected stop/scroll anchor.

### 16. Smart Home Remote — Downloadable — A G C N V

Core screens:

- room/device list;
- light toggle and dimmer;
- thermostat;
- scene;
- lock confirmation;
- failed-command rollback.

Mock services: dynamic endpoint registry, command acknowledgement, permissions.

Decisive scenario: optimistic light control feels immediate and rolls back on
failure; lock actions always cross a trusted confirmation boundary.

### 17. Live Sports Scores — Downloadable — A G C N O V

Core screens:

- followed games;
- live game;
- scoring timeline;
- subscription settings;
- completed-game state.

Mock services: snapshot, bursty score stream, subscription, haptic.

Decisive scenario: replay a burst of updates without flooding rendering or
losing the latest revision, then terminate the ongoing surface at game end.

### 18. Pass and QR Wallet — Trusted — A G C N V

Core screens:

- pass list;
- boarding/membership pass;
- QR/barcode presentation;
- brightness override;
- expired/revoked state;
- signed-update review.

Mock services: asset store, relevance rules, brightness, signed update verifier.

Decisive scenario: render a pass completely offline and reject an untrusted
replacement without losing the last verified asset.

### 19. Remote Control Lab — Trusted — A G V

Core screens:

- discovered targets;
- Find Phone;
- camera shutter;
- presentation clicker;
- acknowledgement/retry;
- disconnected state.

Mock services: discovery, low-latency command transport, acknowledgement ledger.

Decisive scenario: repeated taps and delayed acknowledgements never double-fire
the remote action, and disconnect/recovery is visually unambiguous.

### 20. Snake — Downloadable — A G C N

Core screens:

- primary game/pet scene;
- one-touch interaction;
- inventory/status;
- compact Home state;
- offline background simulation;
- saved-state/update recovery.

Mock services: deterministic simulation clock, haptic/audio cues, save journal.

Decisive scenario: remain smooth under animation, save immediately, consume no
background UI CPU, and preserve state across hot app replacement.

## 8. Permanent artifacts for every app

Each app directory must eventually contain:

```text
apps/<app>/
├── manifest.json
├── appspec/
│   ├── <screen>.json
│   └── surfaces.json
├── fixtures/
│   ├── baseline.json
│   ├── empty.json
│   ├── stale.json
│   ├── offline.json
│   └── error.json
├── scenarios/
│   ├── primary-flow.json
│   └── lifecycle.json
├── src/                    # when a Wasm guest owns behavior
└── README.md
```

The checked-in `evidence/conformance/<app>.json` output contains:

- RGB565 framebuffer hashes and changed-pixel counts;
- semantic-tree snapshots;
- scripted semantic actions;
- native object count;
- LVGL tree depth;
- mounted semantic-node and event counts;
- Wasm module, package, and AppSpec sizes;
- semantic-event and provider-request totals.

Long-duration heap, audio, sensor, and power traces remain hardware-lane
artifacts rather than claims of this desktop pass.

## 9. AppSpec and component evolution

The suite may extend the public language, but it must not add a generic native
widget or style escape hatch.

Expected semantic additions:

- multi-screen routes and route parameters;
- typed control event payloads;
- keyed collections and list-item templates;
- choice groups and split-selection rows;
- compact charts and timelines;
- media status/control;
- notification and ongoing-activity projections;
- map-free navigation maneuver and breadcrumb models;
- pass/QR asset references;
- provider freshness/status;
- bounded local asset references;
- surface declarations in package metadata;
- bounded structural mutations and working state bindings.

Keep the AppSpec v1 framing and canonical encoding while additions remain
backward-compatible. Negotiate component-set capabilities explicitly. Introduce
a new schema version only when an incompatible representation is necessary.

Every schema change requires:

- JSON Schema and CDDL updates;
- Python and native validation;
- malformed/truncated/noncanonical fixtures;
- quota accounting;
- semantic-tree behavior;
- native renderer support;
- a golden and scripted interaction;
- firmware compilation.

## 10. Cross-suite scenario matrix

Every relevant app runs these scenario classes:

| Scenario | Required assertion |
|---|---|
| Baseline | Core flow is interactive and semantic tree is valid |
| Empty | Useful empty state with a next action |
| Loading | Bounded placeholder or progress; no indefinite fake percentage |
| Stale | Age and last-known data remain visible |
| Offline | Local actions remain coherent and queued work is explicit |
| Permission denied | Explanation and recovery path are trusted and human-readable |
| Provider error | No partial state commit; retry is idempotent |
| Burst | Latest revision wins without event/render flooding |
| Background/restore | State, focus, and scroll anchor restore correctly |
| Reboot | Journaled state and scheduled work restore exactly once |
| Guest crash/hang | Home, Voice, and recovery remain responsive |
| Theme/reduced motion | Readable RGB565 output and equivalent semantics |
| Boundary data | Long Unicode, maximum items, extrema, and empty strings are safe |

Global invariants:

- apps remain inside the 240×240 surface;
- only the UI task mutates LVGL;
- accepted transactions are atomic;
- no committed semantic action is silently dropped;
- bound updates do not remount stable subtrees;
- idle screens stop invalidating;
- system UI always outranks guest UI;
- every interactive semantic node has a label and action;
- no provider response bypasses capability or permission checks.

## 11. Implementation waves

### Wave 0 — Harness and OS plane

Build:

- deterministic clock/lifecycle controller;
- scenario file format and runner;
- typed provider registry;
- shared surface model and revision rules;
- Home/watch face/Live Card shell;
- notification and ongoing hosts;
- Voice state gallery and interruption shell;
- App Manager/control-center gallery;
- semantic snapshot and resource-report output.

Exit gate:

- one seeded domain revision renders as full app, Home card, complication,
  notification, ongoing surface, and Voice action;
- a scenario can background, sleep, reboot, disconnect, and recover without
  real time or network;
- system routes survive a trapped guest.

### Wave 1 — Framework baseline, apps 1–6

Build Timer, Weather, Notifications, Tasks, Calculator, and Calendar.

Primary substrate:

- scheduling and time semantics;
- notification host;
- keyed lists;
- dense input;
- provider cache/freshness;
- multi-screen navigation;
- first cross-surface revision tests.

Exit gate:

- every app exposes all declared surfaces;
- the six decisive scenarios pass deterministically;
- no new UI requirement escapes into raw LVGL/AppSpec styling.

### Wave 2 — Product thesis, apps 7–10

Build Workout, Calories, Voice Notes, and Medication/Habits.

Primary substrate:

- shared typed data;
- structured voice transactions;
- ongoing sessions;
- audio lifecycle;
- exact audit history;
- generated-app replacement and state preservation.

Exit gate:

- voice can create, review, correct, and commit app data;
- app state survives replacement, interruption, and simulated reboot;
- trusted audio/session services remain responsive with guest failure.

### Wave 3 — Privileged services, apps 11–16

Build Sensor Recorder, Sleep, Media, Navigation, Transit, and Smart Home.

Primary substrate:

- high- and low-rate provider streams;
- bounded charts;
- long-running service sessions;
- optimistic commands with acknowledgement;
- permissioned sensitive actions;
- external-location and cache behavior.

Exit gate:

- provider streams cannot flood UI or audio;
- long fixtures show no retained-object or heap drift;
- hazardous actions always use trusted confirmation.

### Wave 4 — Scale, security, and delight, apps 17–20

Build Sports, Pass Wallet, Remote Control Lab, and Microgame.

Primary substrate:

- burst coalescing;
- trusted offline assets;
- idempotent low-latency remote actions;
- animation and persistent guest state.

Exit gate:

- burst, asset-integrity, duplicate-command, and hot-update scenarios pass;
- Home and control center remain smooth with all 20 apps installed.

### Wave 5 — Integrated stress lane

Run:

- all 20 installed;
- six concurrent timers/ongoing sessions;
- notification and score bursts;
- active Voice interruption;
- theme swap and reduced-motion toggle;
- provider disconnect/reconnect;
- guest crash/hang;
- storage pressure and rollback;
- simulator soak followed by CoreS3 trace.

Exit gate:

- no system-plane loss;
- no queue overflow or partial transaction;
- no stationary redraws after settling;
- no unexplained heap/object drift;
- latest revisions converge across surfaces;
- every failure names the responsible app, provider, route, and scenario.

## 12. Recommended first implementation issues

1. Define the scenario JSON contract and deterministic clock.
2. Define versioned provider request/event envelopes.
3. Define one shared domain-surface projection contract.
4. Build the Home/watch-face and Live Card route.
5. Build notification and ongoing-activity hosts.
6. Build the complete Voice state gallery and interruption harness.
7. Build App Manager/control-center galleries and telemetry view.
8. Add multi-screen package routing and lifecycle restoration.
9. Add keyed collection binding and list templates.
10. Implement Timer as the first cross-surface lifecycle fixture.
11. Upgrade Calculator into the first complete downloadable fixture.
12. Add suite-wide semantic snapshots and resource reports.

## 13. Decisions locked for Wave 0

### A. Required depth

Decision: all 20 will be fully interactive above mocked providers. Production
networking, BLE, and audio services are not part of this pass.

Screenshot-only breadth does not satisfy the milestone.

### B. Package realism

Decision: all 20 are separate packages sharing SDK/provider/scenario libraries
and discovered through the OS launcher. Downloadable candidates become real
Rust/Wasm packages. Privileged behavior stays behind real host interfaces with
fake providers. The OS-level work is part of the main trusted watch
application, not a guest package.

### C. Surface completeness

Decision: implement every declared surface for every app, even when the surface
is intentionally tiny. One versioned domain state drives app, Home,
notification, ongoing, complication, and Voice projections.

### D. Home interaction model

Decision: start with the proposed watch-face root gestures and three native
Live Card templates: metric/glance, ongoing activity, and alert/quick action.
Apps provide bounded models, not miniature AppSpec trees. Hardware shortcuts
may mirror gestures without changing their semantic actions.

### E. Hardware residency

Decision: the exhaustive first pass runs on desktop. CoreS3 testing uses
rotating selected fixtures and scenarios. Simultaneous on-device residency is
not a gate for this UI conformance phase.

### F. Microgame rendering boundary

Decision: the first microgame is Snake. Its game field uses a bounded
renderer-neutral `canvas` node whose draw-command stream is emitted by the
Wasm app. Wear Compose consumes the stream with native `Canvas`; LVGL consumes
the same stream through its canvas facilities. Material components remain
appropriate for system-owned score, pause, and game-over overlays, but the
board is not decomposed into faux cards. The command vocabulary, buffer,
assets, update rate, dirty regions, and memory are fixed and measured; normal
form-based apps do not receive a general rendering escape hatch.

## 14. Implementation policies already recommended

### AppSpec evolution

Recommendation: extend the narrow semantic vocabulary when a second real app
proves a reusable need; never add a generic widget/style escape hatch. Keep the
v1 framing for backward-compatible additions and negotiate component sets.

Alternative: freeze the current vocabulary and render difficult apps natively.
That would make the screenshots easier while avoiding the generated-app thesis.

### Persistence realism

Recommendation: build a deterministic native journal and simulated reboot
first, then reuse its interface for flash-backed storage. This gives exhaustive
tests without wearing flash or depending on real elapsed time.

Alternative: write directly to real flash from the start. That improves early
hardware realism but makes failure injection and repeatable suites much harder.

### Visual matrix

Recommendation: require baseline dark RGB565 goldens for every state, then add
one alternate theme plus reduced-motion/large-text coverage to representative
screens and every new component.

Alternative: golden every state under every theme. That produces a very large,
low-signal image set before theme authoring and typography are final.

## 15. Onboard storage and CoreS3 input strategy

### Flash before microSD

CoreS3 SE provides 16MB onboard flash and 8MB PSRAM. The initial ESP-IDF
configuration used the stock single-app table:

```text
nvs       24KiB
phy_init   4KiB
factory    1MiB
remainder  unpartitioned
```

The current firmware is about 0.9MiB, so “13% free” describes the selected 1MiB
factory partition, not the whole flash chip.

The repository now uses this custom 16MB partition table:

```text
nvs         16KiB
ota data     8KiB
phy init     4KiB
firmware A   3MiB
firmware B   3MiB
packages  9.94MiB  wear-levelled FAT
```

The firmware mounts `packages` first, reports capacity, and looks for the
activated `/packages/active.wasm` before considering microSD or the embedded
recovery guest. The current 0.91MiB firmware has about 70% free in either
firmware slot. A physical boot on the connected CoreS3 SE reported
`10060 KiB free / 10060 KiB` for the package volume, found and tested 8MiB
Quad PSRAM, loaded the embedded recovery package with no SD card present,
instantiated WAMR, mounted AppSpec, and reached steady state with roughly
8.4MB free heap.

An SD card is therefore not required for the desktop-first suite or initial
on-device package work. microSD remains useful later for bulk audio, exports,
large fixture libraries, development recovery, and removable user data. It is
an expansion tier, not a prerequisite for Wave 0.

The package suite must still enforce storage budgets. Available flash is not
permission to ship unbounded assets or retain every fixture on-device.

### Hardware shortcuts

CoreS3 SE has a PMIC power button, a reset button, and a capacitive controller
whose touch area extends below the 320×240 LCD. M5Unified exposes the lower
touch strip as `BtnA`, `BtnB`, and `BtnC`.

Initial mapping:

- `BtnA`: Back, equivalent to swipe right;
- `BtnB` click: Home; click again on Home opens the launcher;
- `BtnB` hold: open trusted Voice;
- `BtnC`: open Live Cards or invoke the current bounded context action;
- PMIC power click: wake/sleep;
- reset: firmware reset/download only, never an application action.

All button and gesture paths dispatch the same semantic system actions. The
simulator exposes keyboard equivalents so scripts do not depend on physical
hardware.

## 16. Definition of suite completion

The UI-layer conformance milestone is complete when:

- all 20 apps satisfy the full-app definition above;
- all three OS planes are functional and host-owned;
- all declared surfaces derive from coherent versioned domain state;
- decisive scenarios and the cross-suite stress lane pass deterministically;
- every exposed component and AppSpec addition has validation, semantics,
  goldens, interactions, and resource evidence;
- a trapped guest, denied permission, stale provider, failed command, or
  simulated reboot cannot strand Home, Voice, or recovery;
- the simulator artifacts and CoreS3 traces make visual, lifecycle, memory,
  render, and event regressions attributable.
