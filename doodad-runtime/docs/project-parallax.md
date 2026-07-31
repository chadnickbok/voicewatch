# Project Parallax

## Perfect Render and dual-renderer conformance plan

| Field | Value |
|---|---|
| Status | Active plan; foundational oracle and conformance lanes exist |
| Last updated | 2026-07-30 |
| Primary target | Twenty state-aligned Compose-to-LVGL comparisons |
| Stable Material reference | Wear Compose Material 3 `1.6.2` |
| Product renderer | LVGL `9.5.0`, RGB565, 240×240 square |
| Behavior runtime | WAMR `2.4.0` executing the production Rust/Wasm guests |

## Why “Parallax”

Parallax is the apparent displacement of the same object when viewed from two
different positions. Project Parallax renders the same resolved Doodad scene
through two independent views:

- Google’s Wear Compose Material 3 implementation, used as the behavioral and
  stylistic reference; and
- Doodad’s production LVGL implementation, used on the simulator and watch.

The displacement between those views is the signal. It identifies differences
in hierarchy, geometry, emphasis, tokens, typography, state communication,
touch targets, motion, and rasterization.

“Perfect Render” is the name of the reference and comparison workflow. It does
not mean that unrelated rasterizers must produce identical bytes. It means
that every comparison has identical semantic input and enough evidence to
explain and deliberately resolve each difference.

## Outcome

Project Parallax will make this command possible:

```bash
./doodad perfect-render \
  --suite all-20 \
  --profile watch_square_240 \
  --output target/perfect-render
```

The command will:

1. build and execute each real Wasm app under the production WAMR host;
2. drive deterministic semantic actions, provider events, and virtual time;
3. record a renderer-neutral trace of each accepted AppSpec mount and update;
4. replay the same resolved scene revision into LVGL and Wear Compose;
5. capture pixels, semantics, component bounds, tokens, and resource evidence;
6. generate a browsable side-by-side comparison report; and
7. fail automated gates for objective contract violations while leaving
   geometry adaptations and visual-baseline changes subject to human review.

The first product milestone is one aligned resting state for all twenty apps.
The subsequent milestone covers all existing decisive-flow stages.

## Tracking convention

- `[x]` complete and verified in the repository.
- `[ ]` not complete.
- **Gate:** an objective exit criterion required before the next milestone.
- **Review:** a deliberate human judgment that cannot be reduced to one image
  metric.

Checked items must name durable evidence. A local screenshot or one successful
manual run is not sufficient unless the item explicitly describes a manual
review.

## Progress summary

| Milestone | Deliverable | Status |
|---|---|---|
| P0 | Existing Compose oracle and twenty-app conformance foundation | Complete |
| P1 | SceneTrace and SceneSnapshot contracts plus committed-state export | Complete |
| P2 | Bit-identical LVGL trace replay | Complete |
| P3 | Renderer-independent semantic actions | Complete |
| P4 | AppSpec-to-Compose reference renderer | Static renderer complete; review pending |
| P5 | Twenty state-aligned initial app comparisons | Evidence complete; fidelity review failed/pending |
| P6 | Deterministic batch report and comparison gates | Initial batch tooling complete |
| P7 | Live parallel-rendering simulator | Not started |
| P8 | Full decisive flows, variants, and motion | Trace corpus complete; dual renders pending |
| P9 | Reviewed API 37 runtime authority | Eighteen apps captured on square API 37 AVD; suite pending |
| P10 | CI, baseline approval, and hardware closure | Local lane partial |

## Current baseline

### Existing Doodad application evidence

- [x] Twenty separate conformance apps are cataloged in
  [`apps/conformance-suite.json`](../apps/conformance-suite.json).
- [x] All twenty apps execute as real Rust/Wasm guests under WAMR.
- [x] The suite contains 83 authored AppSpec documents: twenty initial screens
  and 63 additional screens.
- [x] Existing decisive flows contain 85 actions and 105 total stages.
- [x] Those stages represent 75 distinct screen IDs and 96 distinct semantic
  states.
- [x] Every decisive flow has a deterministic final RGB565 framebuffer hash in
  [`tests/test_app_flow_goldens.py`](../tests/test_app_flow_goldens.py).
- [x] Every stage already records semantic and resource evidence through
  [`tools/generate_conformance_evidence.py`](../tools/generate_conformance_evidence.py).
- [x] The native host exposes the RGB565 framebuffer, virtual clock, provider
  delivery, semantic tree, object count, and interaction helpers.

### Existing Compose oracle evidence

- [x] The Wear reference lab is buildable under
  [`reference/android-wear`](../reference/android-wear/README.md).
- [x] Wear Compose Material 3, Foundation, Navigation, and tooling are pinned
  to `1.6.2`.
- [x] Ten manually authored reference patterns exist.
- [x] Thirty Roborazzi host goldens cover small round, large round, and
  `watch_square_240`.
- [x] Each oracle golden asserts declared semantic IDs and labels.
- [x] API 37 emulator setup, install, capture, recording, and single-pair
  comparison scripts exist.
- [x] The renderer-neutral reference-scenario contract exists at
  [`contracts/reference-render-scenario-v1.schema.json`](../contracts/reference-render-scenario-v1.schema.json).

### Current gap

- [x] SceneTrace records accepted AppSpec mounts and CommandBatches with
  renderer-neutral snapshots and causal semantic actions.
- [x] All 105 checkpoints replay through production LVGL with matching live
  snapshot, semantic, framebuffer, node, event, and zero-Wasm attestations.
- [x] All decisive flows use stable semantic node/action IDs.
- [x] The Compose reference lab renders arbitrary valid corpus snapshots
  through an explicit all-kind registry and a frozen generic pattern policy.
- [x] Twenty rigorously state-aligned initial Compose/LVGL pairs and normalized
  NodeEvidence reports are generated by one command.
- [x] The generated inventory covers 83 documents, 105 checkpoints, 112
  accepted operations, 96 unique snapshots, and all public component kinds.
- [ ] All declared content is not yet intentionally placed in the initial
  square viewport; many Compose actions begin below the fold.
- [ ] LVGL still has root flex-axis, touch-target, typography, max-lines,
  token-evidence, and missing-glyph defects outside the completed calculator
  path.
- [ ] 94 of 105 checkpoints have reviewed dual-renderer captures.
- [ ] The fast Compose lane still uses Robolectric SDK 33; API 37 runtime
  baselines are reviewed for Timer through Sleep plus Media, Navigation, and
  Transit, Smart Home, Sports, and Wallet, with Remote Control and Snake still
  pending.

Measured results and the remediation order are in
[`project-parallax-comparison-report.md`](project-parallax-comparison-report.md).

## Architectural rule: execute once, render twice

The precise runtime relationship is:

```text
real Wasm package
      |
      v
WAMR behavior host
  - virtual clock
  - deterministic providers
  - scheduler
  - state and navigation
      |
      v
renderer-neutral SceneTrace
  - canonical AppSpec mounts
  - accepted CommandBatches
  - resolved SceneSnapshots
  - semantic causes and revisions
      |
      +--------------------------+
      |                          |
      v                          v
LVGL product renderer      Wear Compose reference renderer
RGB565 / 240 square        Material 3 / round and square profiles
      |                          |
      +-------------+------------+
                    |
                    v
 pixels + semantics + bounds + tokens + motion + resources
                    |
                    v
          comparison report and review
```

Wasm executes exactly once for a scenario. Compose does not run a second copy
of the guest and does not reimplement app, provider, scheduler, or navigation
logic. Both renderers consume the same accepted revision.

This establishes three independent authorities:

1. **Behavior authority:** the production Wasm running under the pinned WAMR
   host with deterministic services.
2. **Material authority:** the pinned Wear Compose implementation and reviewed
   API 37 runtime captures.
3. **Product authority:** the production AppSpec/LVGL renderer and physical
   Doodad hardware.

Only renderer-neutral contracts, semantic data, and resolved state are shared.
Compose and LVGL must not share layout calculations, token lookup
implementations, or rasterization code. Sharing those would allow one layout
bug to appear correct in both renderers.

## Contract model

Project Parallax adds four related contracts. It does not replace the existing
lifecycle or reference-scenario contracts.

### SceneTrace v1

`SceneTrace` is the append-only behavioral record produced while a real Wasm
guest executes.

Required trace-level fields:

- schema version;
- app slug, application ID, package hash, and Wasm hash;
- manifest, runtime ABI, AppSpec schema, component-set, and interpretation
  policy hashes;
- AppSpec and component-set versions;
- deterministic scenario ID;
- theme and display-profile IDs;
- locale, time zone, font scale, and reduced-motion mode;
- WAMR, LVGL, reference-renderer, font, icon, theme, and simulator-build pins;
- surface origin: `guest_appspec`, `trusted_surface`, or `hybrid_projection`;
- start and end revisions; and
- ordered timeline entries.

Each timeline entry records:

- monotonically increasing trace revision;
- scenario time;
- causal input: start, semantic action, provider event, timer, lifecycle event,
  or state restoration;
- semantic node ID, action ID, event kind, and typed value when applicable;
- raw canonical AppSpec mount bytes or their content-addressed reference;
- raw accepted CommandBatch bytes or their content-addressed reference;
- resolved scene snapshot hash;
- semantic-tree hash;
- provider and state revision metadata;
- whether navigation remounted the screen; and
- optional capture-phase and animation-clock metadata.

Accepted scene revisions are recorded only after a complete mount or patch
transaction commits. A rejected mount, batch, binding update, or provider
result may produce a failure record, but it cannot increment the committed
scene revision.

Raw CBOR remains available for auditing and replay. The trace must never store
only a screenshot or only an LVGL object tree.

### SceneSnapshot v1

`SceneSnapshot` is a complete renderer-neutral view of one accepted trace
revision. It is exported from the trusted native AppSpec decoder after all
accepted UI changes and bindings have resolved.

Required snapshot fields:

- app and screen IDs;
- trace and scenario revisions in the enclosing trace entry;
- ordered nodes;
- stable node and parent IDs;
- component kind;
- primary and secondary text;
- variant, tone, size, gap, and alignment roles;
- current value, minimum, maximum, step, and key data where relevant;
- visible, enabled, selected, checked, and progress state where relevant;
- semantics and supported semantic actions; and
- presentation metadata that is genuinely renderer-neutral.

The normalized JSON snapshot is the content-addressable interoperability
format consumed by Compose and comparison tooling. Revisions and scenario
time stay in the enclosing trace entry so returning to identical accepted
state produces the same snapshot hash. That hash is recorded beside the raw
AppSpec and CommandBatch artifacts.

Snapshots describe app and domain state, not renderer-local state. Press
phase, focus, scroll anchor, rotary displacement, swipe progress, animation
clock, and other pixel-affecting interaction state belong to a separate
capture-phase record. That distinction allows multiple rendered frames to
refer to one committed SceneSnapshot without pretending that Wasm changed.

### NodeEvidence v1

Both renderers need a common evidence shape for post-layout comparison. It
records:

- SceneSnapshot hash and capture-phase ID;
- stable node ID and parent ID;
- role, label, value, state, and supported actions;
- visible, enabled, selected, and checked state;
- physical-pixel and logical-dp bounds;
- text line count, truncation state, and baseline data where available;
- effective token roles; and
- renderer-specific diagnostic metadata kept outside the shared fields.

Compose merged/unmerged semantics and LVGL object identities are not accepted
as the interoperability format. Each renderer explicitly translates its live
evidence into NodeEvidence.

### PerfectRenderSuite v1

`PerfectRenderSuite` selects the revisions that become reviewed comparisons.

Each entry defines:

- app slug;
- trace and scenario IDs;
- selected revision or named phase;
- geometry profile;
- expected scene-snapshot hash;
- Compose capture mode: host or API 37 runtime;
- LVGL capture mode: simulator or hardware;
- comparison policy;
- any approved geometry adaptation;
- visual mask justification, if one is unavoidable; and
- baseline-review status.

The existing contracts retain their current roles:

- `conformance-scenario-v1` drives lifecycle and provider behavior;
- `reference-render-scenario-v1` supplies presentation, capture, theme, and
  expected-semantic metadata;
- `SceneTrace` records real Wasm outputs;
- `NodeEvidence` normalizes live semantics and post-layout bounds; and
- `PerfectRenderSuite` selects comparable evidence.

## Proposed repository layout

```text
contracts/
├── scene-trace-v1.schema.json
├── scene-snapshot-v1.schema.json
├── node-evidence-v1.schema.json
└── perfect-render-suite-v1.schema.json

reference/
├── perfect-render-suite.json
├── presentation/
│   └── <screen-id>.json
├── traces/
│   └── <app>/<scenario>/
│       ├── trace.json
│       ├── mounts/
│       └── command-batches/
└── android-wear/
    └── app/src/main/java/dev/doodad/reference/ui/appspec/
        ├── AppSpecReferenceRenderer.kt
        ├── ReferenceComponentRegistry.kt
        ├── ReferenceScreenPatterns.kt
        ├── WearRoundGeometry.kt
        └── WatchSquare240Geometry.kt

tools/doodad_cli/
├── scene_trace.py
├── perfect_render.py
└── comparison_report.py

target/perfect-render/
├── index.html
└── <app>/<phase>/
    ├── compose.png
    ├── compose-rgb565.png
    ├── lvgl.png
    ├── side-by-side.png
    ├── overlay.png
    ├── difference.png
    ├── boundaries.png
    ├── compose-nodes.json
    ├── lvgl-nodes.json
    └── metrics.json
```

The reviewed contract, selected-suite metadata, and intentional goldens may be
checked in. Reproducible working output under `target/` remains ignored.
Large trace payloads should be content-addressed so repeated AppSpec mounts do
not duplicate bytes.

## Milestone P0: Preserve and document the foundation

**Status:** Complete

- [x] Pin Wear Compose Material 3 `1.6.2`.
- [x] Establish `wear_round_small`, `wear_round_large`, and
  `watch_square_240` profiles.
- [x] Build ten high-leverage Compose oracle patterns.
- [x] Record 30 host goldens with semantic assertions.
- [x] Provide API 37 AVD setup and capture scripts.
- [x] Provide a single-pair Compose/LVGL difference workflow.
- [x] Establish the twenty-app WAMR/LVGL conformance suite.
- [x] Record deterministic semantic and resource evidence for decisive flows.
- [x] Accept the dual-renderer and geometry-family decisions in ADR-012 and
  ADR-013.

**Gate:** The existing reference-oracle and twenty-app conformance tests pass
independently.

## Milestone P1: Define SceneTrace and SceneSnapshot

**Status:** Complete for v1 · **Engineering size:** Medium

### Contract work

- [x] Add `scene-trace-v1.schema.json`.
- [x] Add `scene-snapshot-v1.schema.json`.
- [x] Add `node-evidence-v1.schema.json`.
- [x] Add malformed, truncated, duplicate-ID, excessive-depth, and unsupported
  component fixtures.
- [x] Add dependency-free Python validation.
- [x] Define canonical JSON serialization and hashing rules.
- [x] Define trace compatibility and migration rules.
- [x] Set explicit maximum trace-entry and snapshot sizes.
- [x] Define committed scene revisions separately from renderer-local capture
  phases.
- [x] Define stale-artifact rules from package, Wasm, schema, interpretation
  policy, theme, font, icon, and simulator hashes.
- [x] Generate a corpus inventory covering component kinds, properties,
  variants, events, states, and dynamically mounted AppSpecs.
- [x] Freeze the exact twenty-app inventory for the first suite.

### Native snapshot export

- [x] Add a renderer-neutral snapshot builder over the accepted
  `WireDocument`.
- [x] Exclude `mounted_object`, LVGL pointers, callbacks, and renderer-owned
  storage from the snapshot.
- [x] Include all resolved strings, values, flags, keys, events, and semantics.
- [x] Export the snapshot through a bounded C API.
- [x] Expose it through `NativeHost` in Python.
- [x] Unit-test every `ComponentKind`.
- [ ] Verify snapshot generation performs no allocation in firmware builds, or
  keep it explicitly desktop-only behind a build flag.

### Instrumentation points

- [x] Record every successful `host_ui_mount()` AppSpec payload.
- [x] Record every accepted UI CommandBatch after full validation.
- [x] Record navigation mounts that return no CommandBatch.
- [x] Record provider, timer, lifecycle, and restoration causes.
- [x] Record the resolved snapshot only after the accepted transaction is
  visible to the renderer.
- [x] Record rejected input and before/after hashes separately without creating
  an accepted revision.
- [x] Record trusted and hybrid surface origins without presenting them as
  guest-owned AppSpec content.

**Gate P1:** A trace from one dynamic app contains enough information to
reconstruct every accepted resolved scene revision without executing its Wasm
again. Ten identical runs produce byte-identical canonical traces, and failed
transactions never advance a committed revision.

## Milestone P2: Prove bit-identical LVGL trace replay

**Status:** Complete · **Engineering size:** Large · **Priority:** Critical
architectural proof

- [x] Add a replay-only host that consumes SceneTrace mounts and transactions
  without loading WAMR.
- [x] Start replay from a fresh process with no state inherited from trace
  recording.
- [x] Preserve transaction ordering and virtual scenario time.
- [x] Reconstruct navigation remounts and in-place patches.
- [x] Render each replayed stage through the production `m3e_lvgl` source.
- [x] Compare live-Wasm and replay-only SceneSnapshot hashes.
- [x] Compare live-Wasm and replay-only semantic hashes.
- [x] Compare mounted node count and mounted event count.
- [x] Compare live-Wasm and replay-only RGB565 framebuffers byte for byte after
  settled captures.
- [x] Exercise timers, provider responses, state bindings, navigation, and
  failed transactions.
- [x] Run the proof over all twenty current decisive flows.
- [x] Prove replay performs no Wasm calls or provider-side app logic.

**Gate P2:** All 105 existing flow stages replay to the same resolved-scene
hash, semantic hash, and settled RGB565 framebuffer as the live WAMR run.

No Compose work beyond isolated component prototypes should be treated as
app-level conformance evidence until this gate passes.

## Milestone P3: Make scenario actions semantic

**Status:** Complete for decisive flows · **Engineering size:** Medium

- [x] Define scenario actions by node ID, action ID, event kind, and typed
  value.
- [x] Compile or migrate visible-label actions in
  [`apps/conformance-flows.json`](../apps/conformance-flows.json).
- [x] Add direct semantic-event dispatch to the native host.
- [x] Retain coordinate and real-widget interaction as a separate test layer.
- [x] Verify that clicking either renderer produces the expected semantic
  action before dispatching it to WAMR.
- [x] Reject ambiguous or stale node/action pairs.
- [x] Record semantic actions as the causal input in SceneTrace.

**Gate P3:** Every decisive flow can execute without locating an element by
visible text, LVGL object identity, Compose test tag, or screen coordinate.

## Milestone P4: Build the AppSpec Compose reference renderer

**Status:** Static renderer complete; geometry review pending · **Engineering size:** Large

### Renderer structure

- [x] Add `AppSpecReferenceRenderer`.
- [x] Parse and validate `SceneSnapshot v1`.
- [x] Pin a versioned AppSpec-to-Material interpretation policy and include its
  hash in every capture.
- [x] Dispatch components through an explicit registry.
- [x] Key Compose nodes by stable AppSpec node ID so patches retain identity.
- [x] Apply complete snapshots first; add incremental patch playback after
  static correctness is established.
- [x] Tag every node with its AppSpec ID.
- [x] Export normalized declared semantics and actual Compose bounds as JSON.
- [x] Keep renderer errors visible and fail captures instead of silently
  substituting generic content.
- [x] Fail closed on unknown component/property combinations.
- [x] Require documented exception-registry entries for any app-ID-specific
  rendering; ordinary app screens must use the shared registry and patterns.

### Component mapping

The 83 current app documents use the mapped kinds shown below. The remaining
public AppSpec kinds are included so the renderer does not immediately become
incomplete.

| AppSpec kind | Reference implementation | Current app usage | Status |
|---|---|---:|---|
| `screen` | `AppScaffold` + `ScreenScaffold`/pattern root | 83 documents | [x] |
| `text` | Wear Material `Text` with semantic type role | 83 documents | [x] |
| `button` | `Button`, `TextButton`, or `CompactButton` by role | 79 documents | [x] |
| `card` | `Card`/`TitleCard` by content role | 73 documents | [x] |
| `stepper` | Wear Material `Stepper` | 4 documents | [x] |
| `keypad` | `ButtonGroup` + compact action family | 2 documents | [x] |
| `live_card` | live/status card plus Material progress | 2 documents | [x] |
| `progress` | linear, circular, or segmented by semantic style | 2 documents | [x] |
| `column` | semantic vertical container | 5 documents | [x] |
| `row` | semantic horizontal container or action group | currently unused | [x] |
| `scroll` | `TransformingLazyColumn`/bounded scrolling pattern | 15 documents | [x] |
| `toggle` | whole-row Wear Material toggle control | 4 documents | [x] |
| `voice_orb` | Doodad-inspired system component using Material tokens | currently unused by suite | [x] |
| `image` | deterministic decoded bitmap with semantic content description | 5 Media documents | [x] |
| `canvas` | renderer-neutral draw-command surface; Compose `Canvas` / LVGL `lv_canvas` | planned for Snake | [ ] |

### Pattern interpretation

- [x] Define a small generic structural pattern vocabulary: status detail,
  action list, metric control, keypad, countdown, weather hero, notification
  stack, task list, calendar agenda, progress dashboard, and empty. Expand it
  with reviewed selection, confirmation, media, navigation, and microgame
  intent as the corpus requires.
- [x] Infer a pattern only when the AppSpec contains unambiguous semantic
  structure.
- [ ] Store underdetermined design intent in a reviewed, renderer-neutral
  presentation sidecar keyed by screen ID.
- [x] Do not add raw coordinates, Compose class names, LVGL names, colors,
  radii, or animation curves to AppSpec.
- [x] Document every current square adaptation separately from the round
  reference policy.

### Geometry

- [x] Keep `wear_round` and `watch_square_240` layout strategies independent.
- [x] Share semantic, color, typography, shape, motion, and interaction roles.
- [ ] Keep safe areas, edge actions, list transformation, curved text, arcs,
  outer margins, and corner behavior profile-specific.
- [x] Assert 240×240 physical output and 192×192dp logical square geometry.

**Gate P4:** Every AppSpec kind used by the twenty apps has at least one
state-aligned Compose/LVGL component story with matching semantics and a
reviewed geometry disposition. The generated corpus inventory reports 100%
mapping coverage or an explicit approved exception for every used
type/property combination.

## Milestone P5: Establish the twenty-app baseline

**Status:** Evidence generated; fidelity review and correction pending ·
**Engineering size:** Medium after P4

Start with each app’s initial accepted scene. Do not compare the current
terminal LVGL frame to a different Compose fixture.

| # | App | Initial trace | Compose square | LVGL replay | Report | Decisive flow |
|---:|---|:---:|:---:|:---:|:---:|:---:|
| 1 | Timer | [x] | [x] | [x] | [x] | [x] |
| 2 | Weather | [x] | [x] | [x] | [x] | [x] |
| 3 | Notifications | [x] | [x] | [x] | [x] | [x] |
| 4 | Tasks | [x] | [x] | [x] | [x] | [x] |
| 5 | Calculator | [x] | [x] | [x] | [x] | [ ] |
| 6 | Calendar | [x] | [x] | [x] | [x] | [x] |
| 7 | Workout | [x] | [x] | [x] | [x] | [x] |
| 8 | Calories / nutrition | [x] | [x] | [x] | [x] | [x] |
| 9 | Voice Notes | [x] | [x] | [x] | [x] | [x] |
| 10 | Medication | [x] | [x] | [x] | [x] | [x] |
| 11 | Sensor Recorder | [x] | [x] | [x] | [x] | [x] |
| 12 | Sleep | [x] | [x] | [x] | [x] | [x] |
| 13 | Media | [x] | [x] | [x] | [x] | [x] |
| 14 | Navigation | [x] | [x] | [x] | [x] | [x] |
| 15 | Transit | [x] | [x] | [x] | [x] | [x] |
| 16 | Smart Home | [x] | [x] | [x] | [x] | [x] |
| 17 | Sports | [x] | [x] | [x] | [x] | [x] |
| 18 | Wallet | [x] | [x] | [x] | [x] | [x] |
| 19 | Remote Control | [x] | [x] | [x] | [x] | [ ] |
| 20 | Snake / persistent microgame | [x] | [x] | [x] | [x] | [ ] |

### Required non-form workloads

Two workloads deliberately exercise rendering paths outside ordinary Material
components:

- [x] **Package image asset path:** Media loads checked-in album artwork and
  Wallet loads a real demo boarding code through content-addressed package
  assets. Wasm scenes identify assets and presentation semantics; both
  renderers independently validate and decode the same source bytes.
  Acceptance requires matching crop/contain mode, bounds, color handling,
  fallback, missing-asset behavior, memory accounting, and RGB565 evidence.
- [ ] **Direct canvas path:** Snake must render its board through a bounded
  `canvas` node driven by renderer-neutral draw commands emitted from Wasm.
  Compose uses its native Canvas and the product uses LVGL's canvas facilities.
  Material remains appropriate for system-owned overlays and actions, but the
  game field is not decomposed into faux Material cards. Acceptance requires
  deterministic pixels, bounded command and memory budgets, semantic input
  actions, and no per-frame AppSpec remount.

For each initial baseline:

- [x] Pair both renderers with the same SceneSnapshot hash.
- [x] Identify renderer-local capture phase separately from scene revision.
- [x] Capture settled Compose RGB888 from the native host PNG.
- [x] Quantize Compose output through the product RGB565 conversion.
- [x] Capture LVGL RGB565.
- [x] Export both normalized semantic trees and component bounds.
- [ ] Verify all declared content is visible and legible.
- [ ] Verify touch targets and text do not escape the product viewport.
- [ ] Eliminate unintended missing-glyph boxes.
- [ ] Assign a fidelity disposition: Exact, Equivalent, Inspired, or Deferred.
- [x] Record pending review state and notes artifacts for intentional square
  adaptation.

**Gate P5:** The generated report contains twenty state-aligned initial
screens. Every row proves a shared SceneSnapshot hash and has reviewed
semantics, bounds, and geometry disposition.

## Milestone P6: Build comparison and reporting tooling

**Status:** Initial host batch complete; runtime/report UX follow-ups pending ·
**Engineering size:** Medium

### Capture normalization

- [x] Enforce a 240×240 comparison viewport without arbitrary post-capture
  stretching.
- [x] Record source density and logical dimensions.
- [ ] Record the pinned emulator/system-image revision when runtime capture is
  selected.
- [x] Pin locale, time zone, font scale, clock, theme, and dynamic-color state.
- [x] Produce both native Compose output and RGB565-quantized Compose output.
- [x] Preserve raw source captures beside normalized output.
- [x] Fail on missing or stale SceneSnapshot hashes.

### Per-pair artifacts

- [x] Compose image.
- [x] Compose RGB565 image.
- [x] LVGL image.
- [x] Labeled side-by-side presentation in the HTML report.
- [x] Difference image.
- [x] 50/50 overlay.
- [x] Component-boundary overlay.
- [x] Semantic comparison.
- [x] Bounds comparison.
- [x] Token-role comparison.
- [x] Metrics JSON.
- [x] Human review notes and approval state.

### Report

- [ ] Generate a static HTML index with app, screen, phase, profile, and status
  filters.
- [x] Add a twenty-pair contact sheet for rapid scanning.
- [ ] Link each summary cell to full evidence.
- [ ] Show the causal event and trace revision.
- [ ] Highlight clipped content, semantic mismatches, missing glyphs, touch
  target failures, and unapproved masks.
- [ ] Keep previous approved output available for regression comparison.
- [x] Fail when trace, snapshot, renderer, or report input hashes are stale.
- [x] Verify two clean runs produce identical normalized inputs and metrics.

### Comparison policy

Hard automated failures:

- differing or missing expected SceneSnapshot hashes;
- semantic ID, role, label, action, enabled, selected, or checked mismatch;
- missing content;
- component bounds outside the viewport;
- undersized interactive targets unless explicitly approved;
- unrecognized missing glyphs;
- nondeterministic output;
- unsupported component substitution; and
- stale or unreviewed baseline metadata.

Diagnostic metrics:

- RGB565 exact changed-pixel count;
- RMSE;
- per-role color distance;
- edge overlap;
- per-node position and size deltas;
- text baseline, line-count, and truncation deltas; and
- hierarchy and emphasis comparisons.

Raw full-screen pixel equality is not a universal gate because Compose and
LVGL use different text and shape rasterizers. Geometry, role, semantic, and
touchability failures must not be hidden behind a broad image tolerance.

**Gate P6:** One command produces a deterministic, browsable report for every
selected suite entry and exits nonzero on objective contract failures.

## Milestone P7: Add the live parallel-rendering simulator

**Status:** Not started · **Engineering size:** Large

Extend the existing `doodad dev` server rather than creating a second behavior
runtime.

### Driver

- [ ] Keep WAMR, providers, scheduler, and scenario clock authoritative in the
  existing native host.
- [ ] Publish the current SceneSnapshot and trace revision over localhost.
- [ ] Accept semantic actions by stable node and action ID.
- [ ] Dispatch each action once.
- [ ] Wait for an accepted revision before updating either renderer.
- [ ] Allow timeline scrubbing through previously recorded revisions without
  re-executing behavior.

### Compose client

- [ ] Allow the Wear reference app to load a snapshot from the host bridge.
- [ ] Acknowledge the revision after Compose has settled.
- [ ] Support host Roborazzi and real-emulator capture modes.
- [ ] Send semantic UI actions back to the canonical driver.
- [ ] Never call app services or execute Wasm itself.

### Browser dashboard

```text
+----------------------+----------------------+----------------------+
| Compose reference    | LVGL product         | Overlay / difference |
|                      |                      |                      |
+----------------------+----------------------+----------------------+
| app | screen | revision | profile | theme | semantics | bounds    |
+--------------------------------------------------------------------+
| start | action | provider | timer | result | previous | next       |
+--------------------------------------------------------------------+
```

- [ ] Show both frames and the live overlay.
- [ ] Display the current app, screen, trace revision, and causal event.
- [ ] Toggle semantic and component-boundary overlays.
- [ ] Select round/square profile and supported state variants.
- [ ] Submit semantic actions from either panel.
- [ ] Prevent clicks while the renderers disagree about the accepted revision.
- [ ] Surface WAMR, snapshot, renderer, and capture failures independently.

**Gate P7:** Clicking an action in either panel causes exactly one Wasm
transition, then both panels display evidence for the same accepted revision.

## Milestone P8: Expand from baselines to application flows

**Status:** Not started · **Engineering size:** Large

### Existing decisive flows

- [ ] Record all twenty flows as SceneTrace v1.
- [ ] Render all 105 stages in LVGL replay.
- [ ] Render every visually distinct accepted stage in Compose.
- [ ] Deduplicate identical scene hashes without losing timeline causality.
- [ ] Review all 93 currently distinct semantic states.
- [ ] Mark the decisive-flow column in the P5 matrix complete only after the
  app has no unresolved objective failures.

### Cross-suite variants

- [ ] Empty.
- [ ] Loading.
- [ ] Stale.
- [ ] Offline.
- [ ] Permission denied.
- [ ] Provider error and retry.
- [ ] Burst updates.
- [ ] Background and restore.
- [ ] Reboot.
- [ ] Guest crash or hang with trusted recovery.
- [ ] Alternate theme.
- [ ] Reduced motion.
- [ ] Large text.
- [ ] Long Unicode and multiline content.
- [ ] Boundary values and empty strings.

### Motion evidence

- [ ] Resting frame.
- [ ] Pressed frame.
- [ ] Defined mid-animation keyframes.
- [ ] End frame.
- [ ] Normal-speed recording.
- [ ] Slowed recording.
- [ ] Interruption and retargeting behavior.
- [ ] Frame timing and invalidated-area evidence.

High-priority motion stories:

- ButtonGroup redistribution;
- TransformingLazyColumn start, middle, and end positions;
- edge-action entrance and exit;
- picker snapping;
- dialog navigation;
- swipe thresholds and cancellation;
- spring interruption; and
- ambient transition.

**Gate P8:** Every existing decisive flow has reviewed cross-renderer evidence,
and each public component has representative state, variant, and motion
coverage.

## Milestone P9: Establish API 37 runtime authority

**Status:** Twenty exact-geometry API 37 app scenes captured and compared;
motion/accessibility normalization/review pending ·
**Engineering size:** Medium · **Large downloads:** Authorized by the project
owner

- [x] Provide deterministic AVD creation script.
- [x] Install Wear OS 7 API 37 Small Round system image.
- [x] Install Wear OS 7 API 37 Large Round system image.
- [x] Install Wear OS 6.1 API 36.1 Small Round compatibility image.
- [x] Create the three named reference AVDs without overwriting user AVDs.
- [x] Create and configure `Wear_OS_Square` as a 240×240, 200 dpi,
  non-round API 37 AVD matching `watch_square_240`.
- [x] Apply and verify the 240×240 Android runtime override required by the
  upstream square skin, including a native 240×240 framebuffer capture.
- [x] Record installed emulator/system-image revisions and AVD names in the
  initial comparison report.
- [x] Pin exact installed system-image package revisions in captured metadata.
- [x] Install the reference app and run the initial 20-app catalog.
- [x] Capture actual runtime screenshots and accessibility XML.
- [ ] Capture normal-speed and slowed motion.
- [x] Compare host oracle output with API 37 runtime output.
- [x] Document expected host/runtime rasterization differences.
- [ ] Promote reviewed API 37 captures to reference evidence.
- [ ] Keep the documented API 36.1 dashed-arc issue out of product
  requirements.

`Wear_OS_Square` is the primary Material runtime authority for app-screen
comparisons. Its app/capture surface shares the product contract exactly after
the scripted display override: 240×240 pixels, 192×192dp, Android density
1.25 / 200 dpi, and square geometry. The upstream skin's unmodified framebuffer
is 360×360. The round AVDs remain secondary authorities for upstream adaptive
behavior, circular system surfaces, edge components, and round-screen-specific
motion.

**Gate P9:** Every component or pattern claimed Exact has reviewed API 37
runtime evidence in addition to host and LVGL evidence.

## Milestone P10: CI, baseline approval, and hardware closure

**Status:** Not started · **Engineering size:** Medium

### Pull-request lane

- [ ] Validate trace, snapshot, and suite contracts.
- [ ] Regenerate selected traces and verify determinism.
- [ ] Run live-Wasm versus LVGL-replay equivalence.
- [ ] Render the twenty host Compose baselines.
- [ ] Render the twenty LVGL baselines.
- [ ] Run semantic and bounds gates.
- [ ] Generate visual reports as CI artifacts.
- [ ] Require explicit approval for baseline changes.
- [ ] Prevent unreviewed masks or fidelity upgrades.

### Scheduled lane

- [ ] Run the complete 105-stage suite.
- [x] Boot and attest the exact-geometry API 37 `Wear_OS_Square` app/capture
  surface.
- [ ] Capture square runtime smoke scenes and secondary round adaptations.
- [ ] Compare host/runtime drift.
- [ ] Track toolchain and upstream Material changes without silently updating
  stable baselines.

### Hardware lane

- [ ] Replay selected traces on CoreS3/T-Watch hardware.
- [ ] Capture frame time, rendered frames, and invalidated pixels.
- [ ] Capture heap, PSRAM, object count, and display-bus evidence.
- [ ] Review physical touch, haptics, speaker/microphone interaction, sunlight
  legibility, and wrist ergonomics.
- [ ] Link hardware evidence to the same trace and SceneSnapshot hashes.

**Gate P10:** A change from semantic AppSpec through Wasm behavior, Material
reference, LVGL rendering, and selected hardware evidence is attributable to
one versioned trace and one reviewed comparison record.

## “Perfect Render 20” definition of done

The initial all-app milestone is complete when:

- all twenty apps execute their real packaged Wasm under the native WAMR host;
- each comparison identifies one accepted trace revision;
- both renderers attest to the same SceneSnapshot hash;
- Compose uses real Wear Material components through the reference registry;
- LVGL uses the exact production renderer;
- all images are 240×240 without arbitrary stretching;
- Compose has a documented native and RGB565-quantized image;
- semantics and bounds are machine-compared;
- no accidental missing-glyph boxes remain;
- every difference has an objective failure, accepted geometry adaptation, or
  documented rasterization disposition;
- one command regenerates the complete report; and
- baseline updates require deliberate review.

This milestone does not require:

- all 93 semantic states;
- all motion keyframes;
- true Android system notifications, Tiles, or complications;
- physical hardware performance evidence for every app; or
- raw pixel equality between text rasterizers.

Those belong to P8 through P10.

## Recommended CLI

The final names may change during implementation, but the workflow should
remain cohesive:

```bash
# Execute the real package and record accepted behavior.
./doodad trace apps/timer \
  --scenario primary \
  --output target/traces/timer-primary

# Prove that trace replay reproduces the live LVGL run.
./doodad trace verify target/traces/timer-primary

# Render one accepted revision in both renderers.
./doodad perfect-render timer \
  --phase start \
  --profile watch_square_240

# Render and compare the first all-app suite.
./doodad perfect-render \
  --suite all-20 \
  --profile watch_square_240

# Include the API 37 runtime lane.
./doodad perfect-render \
  --suite material-smoke \
  --compose-runtime emulator-5554

# Open the live dual-renderer development view.
./doodad dev apps/timer --perfect-render
```

## Fidelity and approval policy

The labels in [`docs/fidelity-matrix.md`](fidelity-matrix.md) remain binding:

- **Exact:** reviewed metrics, states, tokens, motion, API 37 oracle evidence,
  and per-state comparison.
- **Equivalent:** same semantic role and interaction with an approved square
  or hardware adaptation.
- **Inspired:** Doodad-native composition using shared Material roles.
- **Deferred:** intentionally absent with a documented fallback.
- **Planned:** insufficient evidence.

Host Compose output alone cannot earn Exact. A low whole-image RMSE cannot earn
Exact. A screenshot that looks plausible without aligned state, semantics, and
bounds cannot earn Exact.

Approval records should include:

- reviewer;
- date;
- trace and SceneSnapshot hashes;
- renderer version pins;
- profile;
- changed evidence;
- accepted adaptations;
- remaining known differences; and
- fidelity disposition.

## Risks and mitigations

### Trace omits visually relevant state

**Risk:** Compose and replayed LVGL appear aligned while the trace silently
lost a binding, event, or renderer property.

**Mitigation:** retain raw canonical AppSpec and CommandBatch bytes, export the
resolved scene from the trusted decoder, and require bit-identical live/replay
LVGL frames across all current stages before using traces as the bridge.

### Both renderers share the same layout bug

**Risk:** sharing measurement or layout code makes a defect look conformant.

**Mitigation:** share only schema, semantic roles, data, and state. Maintain
independent Compose and LVGL component/layout implementations.

### Generic AppSpec rendering is not good Material composition

**Risk:** a mechanical vertical-column renderer reproduces semantics but not
Material’s intended patterns.

**Mitigation:** use real Material components plus a small, reviewed semantic
pattern vocabulary and renderer-neutral presentation sidecars where AppSpec is
underdetermined.

### Full-screen pixel metrics create noise

**Risk:** font antialiasing dominates the score while a meaningful geometry
defect is hidden.

**Mitigation:** quantize Compose to RGB565, compare semantics and bounds first,
report per-node metrics, keep text rasterization diagnostic, and prohibit broad
unexplained masks.

### Wasm executes independently in both renderers

**Risk:** clocks, providers, random input, navigation, or implementation bugs
cause state divergence that is misdiagnosed as rendering divergence.

**Mitigation:** execute Wasm exactly once and broadcast accepted resolved
revisions.

### Visible-label flows are ambiguous

**Risk:** labels change, wrap, localize, or repeat.

**Mitigation:** identify scenario actions by stable semantic node and action
IDs. Keep coordinate interaction in a separate UI-input test.

### Trace storage grows rapidly

**Risk:** 105 stages plus variants and motion duplicate large payloads.

**Mitigation:** content-address AppSpec and CommandBatch payloads, deduplicate
identical SceneSnapshots, keep reproducible working images under `target/`,
and check in only reviewed suite metadata and required goldens.

### Domain and renderer-local state are conflated

**Risk:** a press, scroll, focus change, or animation sample is recorded as a
new Wasm scene revision, making replay causality and state comparison
misleading.

**Mitigation:** keep committed SceneSnapshot revision separate from
renderer-local capture phase and animation clock. Multiple capture phases may
reference the same scene hash.

### Trusted and hybrid content is attributed to the guest

**Risk:** system-owned or hybrid projections appear to have been emitted by an
AppSpec guest and are compared under the wrong authority.

**Mitigation:** record surface origin explicitly and keep trusted/system
renderers in their own integration lane while sharing trace correlation IDs.

### Evidence silently becomes stale

**Risk:** a Wasm, schema, mapping, theme, font, icon, simulator, or emulator
change leaves apparently green old captures.

**Mitigation:** include all relevant input hashes and version pins in suite
metadata. Treat any mismatch as missing evidence and require regeneration plus
review.

### The reference renderer starts mimicking product limitations

**Risk:** Compose is adjusted to resemble current LVGL rather than Material
intent.

**Mitigation:** pin upstream Material, review API 37 evidence, document square
adaptations, and fix LVGL unless an explicit Equivalent adaptation is
approved.

### System surfaces are mistaken for app rendering

**Risk:** notifications, Tiles, complications, ambient lifecycle, or launcher
behavior are inferred from the AppSpec renderer.

**Mitigation:** keep those as separate runtime/integration lanes. Add
ProtoLayout or true Android packages only when inspecting their actual system
contracts.

## Open decisions

These decisions do not block P1 or P2.

### Presentation metadata location

**Recommended:** begin with reviewed sidecars under `reference/presentation/`.
Promote a field into AppSpec only after at least two apps prove it is stable,
renderer-neutral semantic intent rather than an oracle-specific hint.

### Snapshot transport for live mode

**Recommended:** extend the existing localhost preview server with JSON
revision and semantic-action endpoints. Let the Android reference app connect
to that driver; do not embed WAMR into the Android app.

### Checked-in trace scope

**Recommended:** check in the selected baseline and decisive trace metadata
plus content hashes. Regenerate bulk binary payloads in CI unless a trace is
required to reproduce a historical approved baseline.

### Snake versus virtual pet

The present twentieth conformance app is Snake, serving as the persistent
microgame representative. Snake is also the required direct-rendering
workload: its board is a WASM-driven, renderer-neutral `canvas`, not a grid of
Material components. If a virtual pet is a distinct product requirement, add
its own AppSpec, Wasm guest, fixtures, and flow rather than relabeling the Snake
evidence.

### Exact visual threshold

**Recommended:** do not choose one global threshold before component-bounds
evidence exists. Establish per-evidence metrics from the first twenty aligned
screens, then lock thresholds by component or story.

## Execution order

The critical path is:

1. P1 — contracts and resolved snapshot;
2. P2 — bit-identical LVGL replay;
3. P3 — semantic actions;
4. P4 — Compose AppSpec renderer;
5. P5/P6 — twenty baselines and report;
6. P7 — live dual-renderer simulator;
7. P8 — complete flows, variants, and motion;
8. P9 — API 37 runtime authority; and
9. P10 — CI and hardware closure.

AVD downloads and setup can proceed in parallel with P1 through P4. Emulator
work does not block the trace bridge or the first host-rendered square
comparisons.

The first implementation increment should stop only after P2’s architectural
proof. The first user-visible increment should stop only after P5 and P6
produce the twenty-row report.

## Progress log

### 2026-07-30

- [x] Chose the name **Project Parallax**.
- [x] Established the initial Wear Compose reference lab.
- [x] Added ten oracle scenarios and thirty three-profile host goldens.
- [x] Added semantic assertions and single-pair image comparison tooling.
- [x] Confirmed all twenty apps produce deterministic WAMR/LVGL flow evidence.
- [x] Counted the current coverage: 83 AppSpec documents, 105 flow stages,
  75 screen IDs, and 96 semantic states.
- [x] Agreed on the execute-once, render-twice architecture.
- [x] Authorized downloading the official multi-gigabyte Wear emulator images.
- [x] Wrote the full tracked Project Parallax plan.
- [x] Added SceneTrace, SceneSnapshot, NodeEvidence, and PerfectRenderSuite v1
  contracts plus dependency-free validation and deterministic hashing.
- [x] Preserved complete AppSpec 1.1 semantic data across Python/C++ CBOR and
  regenerated all 83 authored fixtures.
- [x] Recorded 20 decisive traces containing 105 checkpoints, 112 accepted
  operations, and 96 unique snapshots.
- [x] Proved all 105 checkpoints replay through production LVGL with matching
  live-Wasm snapshots, semantics, framebuffer hashes, and zero replay Wasm
  calls.
- [x] Migrated decisive scenarios to stable semantic node/action IDs.
- [x] Added the generic all-kind Wear Compose AppSpec renderer, eleven reviewed
  structural patterns, square/round profiles, and normalized bounds evidence.
- [x] Installed Wear OS 7 API 37 and Wear OS 6.1 API 36.1 ARM64 images and
  created the three named reference AVDs.
- [x] Added one-command twenty-pair capture, RGB565 normalization, semantic,
  bounds, token and quality comparison, overlays, contact sheet, JSON, and
  static HTML.
- [x] Verified two complete twenty-app runs produce byte-identical normalized
  case artifacts and report output.
- [x] Published the measured
  [`Project Parallax comparison report`](project-parallax-comparison-report.md):
  103 semantic nodes aligned, 664 bounds-field differences, 60.17% changed
  pixels, and 58 renderer-local quality findings.
- [x] Chose Media as the deterministic bitmap/multimedia validation app and
  Snake as the WASM-driven direct-canvas validation app; added explicit
  cross-renderer acceptance criteria for both.
- [x] Completed the Timer oracle redesign and decisive-flow review across
  resting, running, and completed states. The square API 37 runtime and LVGL
  product renderer now share exact normalized structure and bounds, pass all
  current quality checks, and carry an approved **Equivalent** disposition in
  [`reference/reviews/timer.md`](../reference/reviews/timer.md).
- [x] Completed the Weather oracle redesign and decisive-flow review across
  current, loading, stale, offline, and recovered states. The large tonal
  conditions surface is fully visible on the API 37 square runtime and LVGL,
  with exact normalized structure/bounds and an approved **Equivalent**
  disposition in
  [`reference/reviews/weather.md`](../reference/reviews/weather.md).
- [x] Completed the Notifications oracle redesign and decisive-flow review
  across two-unread, detail, quick-reply, sent, one-unread, and empty states.
  The square API 37 runtime and LVGL product renderer share exact normalized
  structure/bounds, all three baseline actions meet the 48dp target, and the
  approved **Equivalent** disposition is recorded in
  [`reference/reviews/notifications.md`](../reference/reviews/notifications.md).
- [x] Completed the Tasks oracle redesign and decisive-flow review across the
  two-task, added-task, first-completion, and second-completion states. Wear
  Material `CheckboxButton` and the LVGL whole-row control now share exact
  normalized structure/bounds, while WASM updates checked state in place
  through the extended command protocol. The approved **Equivalent**
  disposition is recorded in
  [`reference/reviews/tasks.md`](../reference/reviews/tasks.md).
- [x] Completed the Calendar oracle redesign and decisive-flow review across
  agenda, event detail, RSVP confirmation, travel time zone, and recovery.
  The app launches directly into a full-screen agenda; its five documents
  select the calendar-agenda pattern by structure alone. Compose and LVGL
  share exact normalized structure/bounds with no quality findings, and the
  approved **Equivalent** disposition is recorded in
  [`reference/reviews/calendar.md`](../reference/reviews/calendar.md).
- [x] Completed the Workout oracle redesign and decisive-flow review across
  active set, rest, committed next set, and saved summary. The app launches
  directly into the full-screen set workflow; its documents select three
  workout patterns by structure alone. Compose and LVGL share exact normalized
  structure/bounds with no quality findings, and the approved
  **Equivalent** disposition is recorded in
  [`reference/reviews/workout.md`](../reference/reviews/workout.md).
- [x] Completed the Calories oracle redesign and decisive-flow review across
  daily dashboard, quick add, committed total, voice review, and over-goal
  states. The app launches directly into the full-screen nutrition dashboard;
  its documents select three nutrition patterns by structure alone. Compose
  and LVGL share exact normalized structure/bounds with no quality findings,
  the styled stepper remains synchronized with WASM updates, and the approved
  **Equivalent** disposition is recorded in
  [`reference/reviews/calories.md`](../reference/reviews/calories.md).
- [x] Completed the Voice Notes oracle redesign and decisive-flow review
  across ready, recording, locally captured, transcript review, and saved
  states. The app launches directly into a full-screen Material record
  affordance; its five documents select the voice-ready and shared live-action
  patterns by structure alone.
  Compose and LVGL share exact normalized structure/bounds with no quality
  findings, and the approved **Equivalent** disposition is recorded in
  [`reference/reviews/voice-notes.md`](../reference/reviews/voice-notes.md).
- [x] Completed the Medication oracle redesign and decisive-flow review across
  due, logged, reminder editing, due-after-save, and snoozed states. The app
  launches directly into a title-free dose surface; its five documents select
  the shared live-action pattern by structure alone. Compose and LVGL share
  exact normalized structure/bounds with no quality findings, and the approved
  **Equivalent** disposition is recorded in
  [`reference/reviews/medication.md`](../reference/reviews/medication.md).
- [x] Completed the Media oracle redesign and decisive-flow review across
  ready, playing, offline, and reconciled states. AppSpec 1.2 and the package
  manifest now carry content-addressed image assets; Compose and LVGL
  independently validate and decode the same 96×64 DIMG/RGB565 artwork and
  share a deterministic missing-image fallback. The title-free 240×240
  surface has exact normalized structure/bounds with no quality findings on
  all four checkpoints. The API 37 capture, comparison images, and approved
  **Equivalent** disposition are recorded in
  [`reference/reviews/media.md`](../reference/reviews/media.md).
- [x] Completed the Sensor Recorder oracle redesign and decisive-flow review
  across ready, recording, paused, completed-session, and export-ready states.
  All five title-free documents select the shared live-action pattern by
  structure alone. Compose and LVGL share exact normalized structure/bounds,
  two 48dp controls, and no quality findings at every checkpoint. Google's
  current Health Services exercise sample supplies the live-metric,
  pause/finish, session-summary, and ongoing-activity hierarchy; the Doodad
  fixture keeps its own deterministic XYZ data. The approved **Equivalent**
  disposition is recorded in
  [`reference/reviews/sensor-recorder.md`](../reference/reviews/sensor-recorder.md).
- [x] Completed the Sleep oracle redesign and decisive-flow review across
  last-night, overnight, morning, stage, and seven-night states. All five
  title-free documents select the shared live-action pattern by structure
  alone. Compose and LVGL share exact normalized structure/bounds, two 48dp
  controls, and no quality findings at every checkpoint. First-party Pixel
  Watch/Fitbit and Apple references supply the score, stage, quiet-tracking,
  and history hierarchy; the Doodad fixture remains explicitly non-medical.
  The approved **Equivalent** disposition is recorded in
  [`reference/reviews/sleep.md`](../reference/reviews/sleep.md).
- [x] Completed the Navigation oracle redesign and decisive-flow review across
  route-ready, maneuver, cached-GPS, and recovered-GPS states. All five
  authored documents are title-free and select the shared live-action pattern
  by structure alone; the decisive flow accepts four of them. Compose and LVGL
  share exact normalized structure/bounds, two 48dp controls, and no quality
  findings at every accepted checkpoint. The comparison exposed and fixed a
  missing decimal glyph in the shared 32px LVGL live-action font. Google's
  first-party Wear Maps imagery supplies the maneuver, arrival, and offline
  hierarchy. The approved **Equivalent** disposition is recorded in
  [`reference/reviews/navigation.md`](../reference/reviews/navigation.md).
- [x] Completed the Transit oracle redesign and decisive-flow review across
  nearby, full-departure, cached, recovered, and service-delay states. All
  five title-free documents select the shared live-action pattern by structure
  alone. Compose and LVGL share exact normalized structure/bounds, two 48dp
  controls, and no quality findings at every checkpoint. The first comparison
  caught and removed stale-state context/action truncation. First-party
  Citymapper and Apple watch imagery plus current Google Wear guidance supply
  the next-arrival, platform, later-departure, and disruption hierarchy. The
  approved **Equivalent** disposition is recorded in
  [`reference/reviews/transit.md`](../reference/reviews/transit.md).
- [x] Completed the Smart Home oracle redesign and decisive-flow review across
  favorite, light-detail, provider-rollback, retry, hazardous-confirmation, and
  unlocked states. All five title-free documents select the shared live-action
  pattern by structure alone. Compose and LVGL share exact normalized
  structure/bounds, two 48dp controls, and no quality findings at every
  checkpoint. The first comparison caught and removed long-label truncation
  and added a real, regression-tested `%` glyph to the shared LVGL headline
  font. First-party Google Home and Apple Home watch imagery supplies the
  favorite-device, brightness, and secure-action hierarchy. The approved
  **Equivalent** disposition is recorded in
  [`reference/reviews/smart-home.md`](../reference/reviews/smart-home.md).
- [x] Completed the Sports oracle redesign and decisive-flow review across
  live, following, coalesced score-update, final, and scoring-play states. All
  five title-free documents select the shared live-action pattern by structure
  alone. Compose and LVGL share exact normalized structure/bounds, two 48dp
  controls, and no quality findings at every checkpoint. The first comparison
  caught a missing separator glyph and four long-label truncations.
  First-party Google and Apple live-update imagery plus a real Wear baseball
  app supply the score, inning, outs/runners, and latest-play hierarchy. The
  approved **Equivalent** disposition is recorded in
  [`reference/reviews/sports.md`](../reference/reviews/sports.md).
- [x] Completed the Wallet oracle redesign and decisive-flow review across
  boarding-ready, code, unsafe-update rejection, verified-pass recovery, and
  issuer-review states. Four title-free documents select the shared
  live-action pattern; the code document selects the structural `wallet_qr`
  pattern. Compose and LVGL independently verify and decode the same
  content-addressed 135×135 RGB565LE DIMG boarding code, share exact normalized
  structure/bounds, two 48dp controls, and no quality findings at all six
  checkpoints. The first comparison caught and removed a long rejected-update
  label. Google and Apple Wallet references supply the pass hierarchy, and
  API 37 runtime evidence includes the real scan surface. The approved
  **Equivalent** disposition is recorded in
  [`reference/reviews/wallet.md`](../reference/reviews/wallet.md).

Future progress entries should identify the completed milestone, commit or
pull request, tests run, generated evidence, and any decision changed.

## Related documents

- [`docs/project-parallax-comparison-report.md`](project-parallax-comparison-report.md)
  — measured initial twenty-app result and prioritized remediation.
- [`docs/dual-renderer-conformance.md`](dual-renderer-conformance.md) — current
  evidence levels and geometry policy.
- [`docs/appspec-v1.md`](appspec-v1.md) — semantic AppSpec contract.
- [`docs/20-app-conformance-suite.md`](20-app-conformance-suite.md) — complete
  application and lifecycle scope.
- [`docs/fidelity-matrix.md`](fidelity-matrix.md) — fidelity labels and current
  component dispositions.
- [`docs/architecture-decisions.md`](architecture-decisions.md) — locked
  architectural decisions.
- [`docs/simulator.md`](simulator.md) — current WAMR/LVGL simulator and preview
  contracts.
- [`docs/material3-expressive-lvgl-implementation-plan.md`](material3-expressive-lvgl-implementation-plan.md)
  — original component, oracle, and testing plan.
- [`contracts/conformance-scenario-v1.schema.json`](../contracts/conformance-scenario-v1.schema.json)
  — lifecycle and provider-event scenario contract.
- [`apps/conformance-flows.json`](../apps/conformance-flows.json) — current
  decisive action sequences.
- [`reference/android-wear/README.md`](../reference/android-wear/README.md) —
  current Compose oracle stack and commands.
