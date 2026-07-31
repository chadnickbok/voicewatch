# Project Parallax: initial 20-app comparison report

Status: **implemented baseline; fidelity review pending**
Evidence date: **2026-07-30**
Profile: **`watch_square_240` — 240×240 physical pixels, 192×192dp**
Suite: **20 resting initial scenes**

## Executive result

Project Parallax now works as an execute-once, render-twice conformance
pipeline.

The real Doodad packages execute under WAMR, accepted AppSpec mounts and
transactions are recorded, and a renderer-neutral `SceneSnapshot` is replayed
through:

1. a Wear Compose Material 3 reference renderer; and
2. the production LVGL 9.5 renderer.

One command captures both sides, validates their shared snapshot identity,
compares pixels and normalized node evidence, and writes a static report:

```bash
./doodad perfect-render \
  --suite all-20 \
  --profile watch_square_240 \
  --output target/parallax/perfect-render-20
```

The result is architecturally successful and visually incomplete:

- **20/20** cases captured from the same accepted snapshot on both sides.
- **20/20** LVGL replays matched their live-Wasm checkpoints.
- **0** Wasm calls occurred during replay-only capture.
- **103/103** normalized contract projections matched in identity, hierarchy,
  declared semantics, state, actions, and the current token projection.
- **0** semantic, hierarchy, state, action, or token comparison mismatches.
- **664** coordinate mismatches: 332 physical-pixel fields and 332 logical-dp
  fields.
- **693,160 of 1,152,000 pixels changed**, or **60.17%** after converting the
  Compose reference through the canonical RGB565 product conversion.
- **58** renderer-local quality findings: 34 in the current Compose
  interpretation and 24 in LVGL evidence.
- **20/20** matching snapshots also ran on the exact-size square Wear OS 7 /
  API 37 emulator; host/runtime drift was **12.92%** of RGB565 pixels.
- **0/20** screens are ready for an Exact or Equivalent fidelity label.
  Every baseline remains **Planned / pending review**.

The key conclusion is:

> The shared-state bridge is no longer the problem. Semantic intent,
> square-screen composition, typography, and LVGL layout behavior are now the
> dominant gaps.

Open the generated
[HTML report](../target/parallax/perfect-render-20/report/index.html),
[contact sheet](../target/parallax/perfect-render-20/report/contact-sheet.png),
or [machine-readable results](../target/parallax/perfect-render-20/report/report.json).
Working images under `target/` are intentionally not checked in until a
baseline is reviewed.

## What “official Material version” means here

Google has not designed official Timer, Snake, Medication, or other Doodad
screens. Calling these “official Google app versions” would overstate the
evidence.

The reference side is instead:

> **Wear Compose Material 3 reference render — Project Parallax mapping**

It uses Google's real Wear Compose Material 3 `1.6.2` components, theme,
typography, shapes, and host rasterization. Project Parallax supplies the
versioned policy that maps renderer-neutral Doodad semantics onto those
components.

That makes the reference authoritative for component behavior and Material
roles. It does not make every Project Parallax composition a Google-authored
screen. Composition decisions that AppSpec does not express remain Doodad
design decisions.

Relevant upstream references:

- [Wear Compose releases](https://developer.android.com/jetpack/androidx/releases/wear-compose)
- [Build Wear UI with Compose](https://developer.android.com/training/wearables/compose)
- [Migrate to Material 3 for Wear](https://developer.android.com/training/wearables/compose/migrate-to-material3)
- [Wear design language](https://developer.android.com/design/ui/wear/guides/get-started/design-language)
- [Adaptive Wear design](https://developer.android.com/design/ui/wear/guides/foundations/adaptive-design)
- [Wear screen-size and screenshot guidance](https://developer.android.com/training/wearables/compose/screen-size)

## Implemented stack

```text
real Doodad package
       │
       ▼
WAMR 2.4.0 + deterministic providers/clock
       │ accepted mount or CommandBatch
       ▼
SceneTrace v1 ──► resolved SceneSnapshot v1
                          │
              ┌───────────┴───────────┐
              ▼                       ▼
Wear Compose Material 3          production m3e_lvgl
1.6.2 host renderer              LVGL 9.5 simulator
              │                       │
      PNG + RGB888 +             RGB565LE +
      NodeEvidence               NodeEvidence
              └───────────┬───────────┘
                          ▼
        native-size comparison and static report
```

The main implementation is in:

- [`scene-trace-v1.schema.json`](../contracts/scene-trace-v1.schema.json)
- [`scene-snapshot-v1.schema.json`](../contracts/scene-snapshot-v1.schema.json)
- [`node-evidence-v1.schema.json`](../contracts/node-evidence-v1.schema.json)
- [`perfect-render-suite-v1.schema.json`](../contracts/perfect-render-suite-v1.schema.json)
- [`perfect-render-suite.json`](../reference/perfect-render-suite.json)
- [`parallax_pipeline.py`](../tools/doodad_cli/parallax_pipeline.py)
- [`AppSpecReferenceRenderer.kt`](../reference/android-wear/app/src/main/java/dev/doodad/reference/ui/AppSpecReferenceRenderer.kt)
- [`ComposeNodeEvidence.kt`](../reference/android-wear/app/src/main/java/dev/doodad/reference/ui/ComposeNodeEvidence.kt)
- [`scene_snapshot.cpp`](../components/m3e_lvgl/src/appspec/scene_snapshot.cpp)

### Pinned capture environment

| Layer | Pin |
|---|---|
| Wear Compose | `1.6.2` |
| Compose reference mode | Robolectric Native Graphics, host |
| Wear runtime | API 37, `Wear_OS_Square`, native 240×240 capture surface |
| Runtime system image | `android-37.0;android-wear-signed;arm64-v8a`, revision 1 |
| Android Emulator | `37.1.11` |
| Compose renderer build | `b8f0e3dbe4b0ba9fcd6b0007ada5d38e85d0ec11038796127404161f9bae55d5` |
| Interpretation policy | `428987a3d78c6690be855678c380428857b3874eb55217279abe7a7f9eec1733` |
| LVGL | `9.5.0` |
| WAMR | `2.4.0` |
| Capture locale | `en-US` |
| Time zone | `UTC` |
| Font scale | `1.0` |
| Density | `1.25` |
| Logical viewport | `192×192dp` |
| Physical viewport | `240×240px` |
| Dynamic color | disabled |
| Reduced motion | enabled |

The Compose build and policy hashes are captured per case. LVGL manifests also
record simulator-source, trace, checkpoint, snapshot, semantic, framebuffer,
and evidence hashes.

## Method and gates

Each suite entry selects an accepted trace sequence and one settled
renderer-local phase. The first suite deliberately selects sequence 0,
revision 1, and `resting` for each app.

The comparator:

1. rejects stale or different snapshot hashes;
2. requires both sides to be exactly 240×240;
3. preserves raw Compose RGB888 and LVGL RGB565LE;
4. quantizes Compose with the production RGB565 conversion;
5. compares every pixel without resizing or masking;
6. compares normalized identity, hierarchy, semantics, state, actions, bounds,
   token roles, and optional text evidence;
7. audits visible bounds and 48dp interactive targets independently on each
   renderer;
8. writes native images, RGB565 reference output, side-by-side, absolute
   difference, 50/50 overlay, boundary overlays, metrics, manifests, review
   state, contact sheet, JSON, and HTML.

Pixel equality is diagnostic, not the definition of Material equivalence.
Different rasterizers, fonts, and antialiasing will prevent useful raw
equality even after geometry converges.

Two complete 20-app runs produced byte-identical case artifacts,
`report.json`, and `contact-sheet.png`.

## Per-app results

“Changed” is the percentage of RGB565 pixels that differ. “Structured” is the
number of normalized evidence mismatches; all are bounds fields. “Compose Q”
and “LVGL Q” are renderer-local geometry/touch findings, not cross-renderer
differences.

| App | Changed | RMSE | Structured | Compose Q | LVGL Q |
|---|---:|---:|---:|---:|---:|
| Calculator | 64.53% | 60.49 | 24 | 0 | 2 |
| Calendar | 62.13% | 118.86 | 32 | 2 | 1 |
| Calories | 58.46% | 114.55 | 40 | 2 | 1 |
| Media | 60.52% | 118.28 | 32 | 2 | 1 |
| Medication | 62.65% | 119.92 | 32 | 2 | 1 |
| Navigation | 59.94% | 116.20 | 32 | 2 | 1 |
| Notifications | 39.17% | 81.03 | 40 | 1 | 3 |
| Remote control | 62.67% | 120.31 | 32 | 2 | 1 |
| Sensor recorder | 61.12% | 116.63 | 32 | 2 | 1 |
| Sleep | 56.52% | 106.82 | 32 | 2 | 1 |
| Smart home | 59.96% | 115.68 | 32 | 2 | 1 |
| Snake | 58.20% | 109.45 | 32 | 2 | 1 |
| Sports | 58.65% | 109.50 | 32 | 2 | 1 |
| Tasks | 77.00% | 134.03 | 32 | 1 | 2 |
| Timer | 62.36% | 120.42 | 40 | 1 | 1 |
| Transit | 60.84% | 114.02 | 32 | 2 | 1 |
| Voice notes | 62.47% | 122.04 | 32 | 2 | 1 |
| Wallet | 63.32% | 122.45 | 32 | 2 | 1 |
| Weather | 53.92% | 100.33 | 40 | 2 | 1 |
| Workout | 58.97% | 90.07 | 32 | 1 | 1 |

These values should not be used to rank app quality yet. Tasks has the largest
pixel delta because its two compositions place and size substantial surfaces
differently. Notifications has the smallest delta, but still contains
material interaction and geometry defects.

## Findings

### 1. The semantic bridge is sound

All 103 normalized evidence entries are present on both sides in the same
order. The current comparison found no differences in:

- node identity or parentage;
- semantic roles, labels, or values;
- visibility, enabled, checked, or selected state;
- semantic action IDs and event kinds; or
- normalized token projection.

This is the most important implementation result. Future visual corrections
can be attributed to layout and rendering rather than independent app
execution or state drift.

There are two evidence caveats:

1. batch semantic fields are projected from the shared snapshot rather than
   exported from each renderer's live accessibility tree; focused Compose
   tests verify tags/actions, and all twenty live Compose trees are now
   independently captured from API 37, but they are not yet normalized into
   the cross-renderer evidence gate; and
2. matching token evidence does not prove that both renderers painted those
   tokens truthfully. Some LVGL evidence currently duplicates and hardcodes
   styling decisions.

Actual semantics export and centralized resolved token evidence are required
before either match becomes a release gate.

### 2. The two renderers make opposite overflow decisions

The Compose mapping uses real Material typography and scrollable square
surfaces. On 15 initial scenes, the primary action is below the initial
viewport and therefore has zero visible bounds in the resting capture. Timer,
Tasks, Notifications, and Workout show partially visible actions.

LVGL instead compresses most initial scenes so all high-level content appears
at once. That makes it look denser than Material and reduces gaps and type
hierarchy.

Neither behavior should be accepted generically:

- a primary action silently below the fold is poor for short generated apps;
- compressing every screen until it fits destroys Material sizing and touch
  targets.

The square policy needs explicit pattern decisions: fit, scroll, pin an edge
action, or move a secondary action to another page.

### 3. `stretch` is a real LVGL layout bug

Every initial root requests `alignment: stretch`. The LVGL renderer maps that
single value to `LV_FLEX_ALIGN_SPACE_EVENLY` and applies it to the main,
cross, and track axes.

That changes every authored root gap:

- Calendar requests 10px-equivalent spacing and measures 13px.
- Tasks requests 15px and measures 9px.
- Workout requests 10px and measures 1px.
- Calculator requests 10px and produces 6px overlaps.

Main-axis arrangement and cross-axis stretching must be resolved separately.
This is the first LVGL layout fix because it contaminates all twenty screens.

### 4. AppSpec typography intent is partly wrong

The policy maps `numeral` to Material `numeralLarge`, currently 50sp. That
correctly creates a large timer, weather value, score, or measurement.

Seven initial AppSpecs use `numeral` for prose:

- `calendar.summary`
- `voice-notes.summary`
- `medication.summary`
- `media.summary`
- `smart-home.summary`
- `wallet.summary`
- `remote-control.summary`

The Compose reference exposes the mistake as huge, clipped prose. LVGL hides
it because 21 typography roles collapse into 10, 14, 16, or 18px Montserrat
fonts.

Those AppSpecs must change to a title/body role before LVGL adopts the correct
Material numeral scale. Otherwise fixing LVGL typography will make seven
screens worse.

### 5. Touch targets are below policy

The normalized high-level audit finds:

- 22 LVGL interactive nodes below 48dp;
- 19 Compose nodes below 48dp in the initial visible capture, mostly because
  they are partially clipped or fully below the scroll viewport.

The LVGL component audit finds a wider issue than high-level NodeEvidence can
express:

- all 19 default buttons are 41.6dp high;
- three compact notification buttons are 32dp high;
- twenty calculator keys are approximately 38–39×32dp; and
- four stepper decrement/increment controls are approximately 33–35×32dp.

Keypad and stepper child targets need their own evidence IDs. Until then, the
machine audit undercounts actual touch failures.

### 6. Calculator needs a square-specific composition

LVGL Calculator is the only initial scene with true physical viewport escape:

- heading: `y=-1..15`;
- result: `y=9..30`;
- keypad: `y=24..244`;
- heading/result and result/keypad each overlap by 6px;
- keypad extends 4px below the screen.

Simply enlarging its twenty keys to 48dp cannot fit. The square profile needs
a reduced primary keypad, a secondary function page, or another reviewed
interaction design.

### 7. The LVGL font repertoire is incomplete

Seventeen apps contain 23 visible U+00B7 middle-dot occurrences. U+00B7 is
absent from every enabled LVGL 10/14/16/18px built-in font, producing tofu
boxes. The Compose host reference renders the separator correctly.

The fix is to generate owned role fonts with the required codepoint repertoire,
not to modify LVGL's managed built-ins or rewrite the semantic strings.

### 8. Text conformance is not yet observable

All 40 direct text nodes omit optional line count, truncation, and baseline
evidence. LVGL ignores all authored `max_lines` values: 26 one-line and 14
two-line declarations in the initial suite.

Both renderers need actual text measurement evidence before line wrapping and
legibility can be gated.

### 9. The first corpus is intentionally shallow

The current runtime corpus contains:

- 20 apps;
- 83 authored AppSpec documents;
- 105 checkpoints;
- 114 accepted operations;
- 96 unique resolved snapshots; and
- 13 public component kinds.

The first report selects only 20 sequence-zero snapshots. The authored corpus
uses only eight of the thirteen kinds; column, row, scroll, toggle, and
voice-orb are absent.

This is enough to validate the architecture and expose global layout defects.
It is not enough to claim all-app conformance.

## Square versus round

Material 3 Expressive for Wear is strongly shaped by circular displays:
curved time text, edge-hugging containers, round safe areas, transforming
lists, arcs, and edge buttons.

The primary API 37 reference AVD is now square and configured to the exact
product display contract: 240×240 physical pixels, 192×192dp, and density
1.25 / 200 dpi. Therefore:

- Wear Compose is the component, behavior, role, and motion oracle.
- `Wear_OS_Square` is the primary API 37 app-screen runtime authority.
- Host and runtime renders can both use `watch_square_240` without resizing.
- API 37 round captures are secondary adaptive/runtime qualification.
- Circular-only geometry is not a requirement for the square product.

The square profile should share color, typography, shape, motion, state, and
interaction roles while separately defining safe areas, scroll strategy,
edge actions, arc placement, list transformations, density, and corner
behavior.

## API 37 runtime status

The local SDK now contains:

- Wear OS 7 API 37 ARM64 system image, revision 1;
- Wear OS 6.1 API 36.1 ARM64 system image, revision 1;
- API 37 platform revision 2;
- Android Emulator `37.1.11.0`, build `15917651`; and
- `Wear_OS_Square`, configured as API 37, 240×240, 200 dpi, `hw.arc=false`,
  with a 240×240 Android runtime display override after boot; plus
- `doodad_wear7_small_round`, `doodad_wear7_large_round`, and
  `doodad_wear61_small_round` AVDs.

The upstream `wearos_square` skin ignored the AVD's 240×240 hardware fields and
started at 360×360. The capture lane therefore applies Android's supported
`wm size 240x240` override while retaining density 200. Native `screencap`
output and the Android accessibility root both attest an exact 240×240
application surface.

The initial runtime qualification now contains:

- 20/20 resting screenshots at native 240×240, without resampling;
- 20/20 Android accessibility XML trees;
- 20/20 unique SceneSnapshot hashes matching the host suite;
- Wear OS 7 / API 37 build
  `google/sdk_gwear_arm64/emu64a:17/CP2A.260330.028.E2/15706224:user/release-keys`;
- system image
  `system-images;android-37.0;android-wear-signed;arm64-v8a`, revision 1;
- Emulator 37.1.11; and
- one pinned installed reference APK hash across the batch.

The native host/runtime comparison changed 148,877 of 1,152,000 RGB565
pixels, or 12.92%, with MAE 16.85 and RMSE 57.23. Per-app change ranged from
4.15% for Calculator to 22.20% for Wallet. This is materially smaller than the
60.17% host-Compose/LVGL delta, but it is not negligible.

Visual review shows that host and runtime make the same broad composition
decisions. The oversized prose-as-numeral heroes, truncation, and bottom-edge
crowding are therefore reference-mapping problems rather than artifacts of
the host screenshot harness. The remaining host/runtime delta is concentrated
in actual Android font measurement, line breaks, ellipsis, and rasterization;
it must remain visible rather than being hidden with resizing or thresholds.

Open the
[API 37 runtime contact sheet](../target/parallax/runtime-wear-square-240-final/contact-sheet.png)
or the
[host/runtime metrics](../target/parallax/runtime-wear-square-240-final/host-runtime-comparison.json).
These working artifacts remain unapproved and are intentionally kept under
`target/`.

Runtime qualification still needs:

- normal- and slowed-motion evidence;
- normalized comparison of Android accessibility XML;
- small- and large-round adaptive smoke scenes;
- the documented API 36.1 dashed-arc exception; and
- explicit review before any runtime baseline is promoted.

## Recommended path forward

### P0 — correct semantic intent and square layout

1. Change the seven prose `numeral` nodes to title/body roles.
2. Split LVGL main-axis arrangement from cross-axis stretch.
3. Preserve authored gaps; never use flex shrink as the generic overflow
   policy.
4. Define fit/scroll/pinned-action behavior for the six structural patterns.
5. Redesign the square Calculator instead of compressing twenty keys.
6. Enforce 48dp hit regions, with only explicit documented compact exceptions.
7. Add owned fonts containing U+00B7 and a corpus codepoint-coverage gate.

Exit gate:

- no visible overlap or physical viewport escape;
- no accidental tofu;
- no undocumented sub-48dp target;
- every initial scene has an intentional first viewport and scroll behavior.

### P1 — make evidence truthful

1. Centralize renderer-neutral resolved token roles and consume the same
   result for painting and evidence.
2. Add line count, truncation, and baselines on both renderers.
3. Enforce `max_lines` and ellipsis in LVGL.
4. Emit child-target evidence for keypad keys and stepper buttons.
5. Add safe-area, overlap, density, and codepoint audits.
6. Store underdetermined composition intent in reviewed renderer-neutral
   presentation sidecars, not app-ID conditionals.

Exit gate:

- semantic/action mismatches remain zero;
- token evidence is derived from actual resolved styles;
- text and every touch surface are machine-observable.

### P2 — expand from 20 initial states to the real product corpus

1. Render all 105 checkpoints.
2. Deduplicate but retain causality for all 96 unique snapshots.
3. Add fixtures for the five missing component kinds.
4. Add empty, loading, stale, offline, error, retry, large-text, long-Unicode,
   alternate-theme, and reduced-motion variants.
5. Add pressed, selected, disabled, and defined motion keyframes.
6. Require review dispositions per state: Exact, Equivalent, Inspired, or
   Deferred.

Exit gate:

- every decisive state and public component kind has aligned evidence;
- no fidelity label is inferred from an initial resting frame.

### P3 — qualify against API 37 and hardware

1. Cold-boot `Wear_OS_Square` in Android Studio, apply the scripted display
   override, and attest API 37, 240×240, 200 dpi, and non-round geometry over
   `adb`.
2. Install the reference app and capture runtime screenshots and accessibility.
3. Compare the exact-size runtime output with the host and LVGL frames without
   resampling.
4. Run secondary small- and large-round adaptive smoke scenes.
5. Replay selected traces on CoreS3/T-Watch hardware.
6. Record frame time, invalidated pixels, memory/PSRAM, display-bus load,
   haptics, touch behavior, sunlight legibility, and wrist ergonomics.

Exit gate:

- reviewed API 37 evidence supports upstream Material claims;
- reviewed hardware evidence supports product performance claims.

## Recommended conformance policy

Do not choose one global pixel threshold from this baseline. Geometry and
authoring errors dominate the score.

Use this order:

1. shared snapshot hash must match;
2. node identity, hierarchy, semantics, state, and actions must be exact;
3. viewport, overlap, touch-target, glyph, and text-legibility audits must
   pass;
4. component bounds receive profile- and component-specific tolerances;
5. color and typography token roles must match actual painted roles;
6. RGB565 pixel metrics remain diagnostic for raster and regression drift;
7. a reviewer assigns the fidelity disposition and documents square
   adaptations.

After P0/P1, rerun these twenty scenes to establish meaningful component-bound
distributions. Only then lock numeric thresholds.

## Decision

Do not tune LVGL pixels directly toward the current screenshots yet.

First correct AppSpec typography intent and the square composition policy.
Then fix LVGL flex alignment, touch geometry, fonts, and observable text
layout. Once both sides express the same reviewed design, pixel and bounds
comparisons become useful regression gates instead of measurements of two
different composition strategies.

Project Parallax has successfully moved the problem from subjective screenshot
imitation to reproducible, attributable evidence. The next phase is no longer
“build the comparison lab”; it is “use the lab to correct the shared design
intent and production renderer.”
