<!-- Saved from the user-provided implementation plan on 2026-07-30. -->

# Material 3 Expressive for Wear on LVGL

## Codex-ready implementation plan for an ESP32-S3 voice-first watch platform

**Status:** implementation specification  
**Target hardware:** LilyGO T-Watch S3; M5Stack CoreS3 SE development target  
**Target stack:** ESP-IDF, LVGL 9.5.x, `esp_lvgl_port`, WAMR, Rust/Wasm generated apps  
**Reference design system:** Wear Compose Material 3 Expressive 1.6.2  
**Document date:** 2026-07-30

---

## 0. The assignment

Build a native LVGL design-system and application runtime that reproduces a large, useful, and visually faithful subset of Material 3 Expressive for Wear on ESP32-S3 hardware.

This is not an LVGL theme with Material-ish colors. It is a product platform with:

- a semantic token system for color, typography, shape, spacing, motion, state, and haptics;
- reusable LVGL components with defined anatomy, variants, states, interactions, and accessibility metadata;
- scaffolds, navigation, transformed lists, pagers, dialogs, pickers, and overlays;
- a versioned declarative UI format that server-generated Rust/Wasm apps can safely drive;
- atomic user-selectable themes generated from color tokens;
- a custom voice interaction layer that feels native to the design language;
- an Android reference renderer and desktop LVGL simulator for visual comparison;
- explicit CPU, memory, frame-time, power, and binary-size budgets;
- hardware-in-the-loop tests on both the CoreS3 SE and T-Watch S3.

The result should make this interaction credible:

> “Create me a calorie tracker. Make protein prominent, use a deep violet theme, and put quick-add at the bottom.”

The server should be able to plan and build the app, validate its UI against a constrained schema, test it, download it, and hot-load it. The firmware should render the result with the same quality and behavior as hand-authored first-party watch apps.

### Non-goals

- Do not run Jetpack Compose, Android, or Kotlin on the watch.
- Do not translate arbitrary HTML/CSS or let generated code place raw pixels.
- Do not expose `lv_obj_t *`, styles, display buffers, or LVGL callbacks to Wasm.
- Do not port the AndroidX implementation line by line.
- Do not chase every round-screen-only feature before the square-screen product works.
- Do not make arbitrary vector morphing, blur, full-screen alpha compositing, Lottie, or runtime variable-font rendering foundational dependencies.
- Do not promise 60 fps for full-screen animation over the T-Watch SPI display.

---

## 1. Decisions to lock before implementation

Record these in `docs/architecture-decisions.md`. A pull request that changes one must include measurements or a concrete compatibility reason.

| Decision | Initial choice | Why |
|---|---|---|
| Material reference | Wear Compose Material 3 **1.6.2** | It is the current stable Wear release. Track 1.7 separately, but do not let alpha API churn define v1. |
| LVGL | Exact **9.5.x** tag, initially 9.5.0 | Stable, current, and supportable. Never quietly compile production against `master`. |
| ESP integration | `espressif/esp_lvgl_port` **2.8.0~1** or the exact tested 2.8 patch release | It provides display/input attachment, locking, LVGL task integration, sleep, and async flush behavior. |
| ESP-IDF | One pinned 5.5.x patch during bring-up | Reduce integration churn. Add an IDF 6.0 compatibility CI job only after the baseline is stable. |
| Pixel format | RGB565 end-to-end on hardware | Matches the panels and DMA path; controls memory and bandwidth. |
| Logical viewport | **192×192dp** mapped to **240×240px** | `1dp = 1.25px`; a 48dp target becomes 60px. This preserves Wear proportions on the final watch. |
| Development viewport | Centered 240×240 square inside CoreS3's 320×240 panel | The development device shows the exact final layout. Side rails are debug-only. |
| Host UI language | C++17 wrapper over the LVGL C API | RAII and typed component APIs without changing LVGL's ABI or requiring Rust in the firmware UI layer. |
| Generated app boundary | Versioned semantic AppSpec, JSON for authoring and canonical CBOR on device | Small, validateable, diffable, and independent of LVGL implementation details. |
| Guest execution | Rust compiled to Wasm under WAMR | Type-safe generated logic; capability-limited host calls. |
| Threading | All LVGL mutation on one UI task, protected by the port lock | LVGL calls from network, audio, WAMR, or storage tasks are forbidden. |
| Palette generation | Material Color Utilities on the server/build host | HCT and contrast work do not belong in the frame loop or scarce firmware flash. |
| Fonts | Prebuilt, subsetted LVGL bitmap fonts | Predictable flash, RAM, and render cost. Static Roboto Flex instances preserve its character without a runtime variable-font engine. |
| Icons | Curated Material Symbols subset | Tintable, measurable, and quota-controlled. |
| Full-screen motion target | 30 fps | Realistic for RGB565 over SPI with enough time left to render. |
| Small-region motion target | 60 fps when dirty area permits | Fast local feedback remains possible without requiring full-frame throughput. |
| Public generated UI | Semantic components only | Generated apps inherit quality, accessibility, theme behavior, and performance limits. |

### Why the 192dp model matters

Wear guidance treats roughly 192–224dp as the compact class and uses 225dp as an important adaptive breakpoint. The T-Watch panel is 240 physical pixels wide. Rendering a 192dp logical canvas at a fixed 1.25 scale gives a clean product contract:

```text
logical dp        physical px
----------        -----------
1                 1.25
4                 5
8                 10
12                15
16                20
24                30
32                40
40                50
48                60
64                80
192               240
```

Use Q8.8 or Q16.16 fixed-point math for scale conversion. Round container edges consistently; distribute accumulated rounding error in layout rather than rounding every child independently. Never let each component invent its own conversion.

Define these display profiles from day one:

```cpp
struct DisplayProfile {
  uint16_t physical_width_px;
  uint16_t physical_height_px;
  uint16_t logical_width_dp;
  uint16_t logical_height_dp;
  uint16_t density_q8_8;
  ScreenShape shape;          // square, round
  InputKind input;            // touch, crown, buttons
  InsetsDp safe_insets;
  bool supports_edge_button;
  bool supports_curved_text;
};
```

Required profiles:

- `watch_square_192`: 240×240px, 192×192dp, final T-Watch profile.
- `cores3_watch_preview`: 320×240px, centered 240×240 watch viewport.
- `wear_round_192_reference`: simulator/reference only, to detect assumptions that unnecessarily block future round hardware.
- `wear_large_225_reference`: simulator only, to exercise adaptive rules.

---

## 2. Source of truth and fidelity policy

Material's public prose is not precise enough to implement every radius, padding, state opacity, and animation. Establish a source hierarchy and automate as much of it as practical.

### Normative hierarchy

1. **Design intent and visual grammar:** the official [Material 3 Expressive Wear design language](https://developer.android.com/design/ui/wear/guides/get-started/design-language), [Wear design hub and Figma kits](https://developer.android.com/design/ui/wear), color, typography, and adaptive-design guidance.
2. **Stable API semantics and supported component behavior:** [Wear Compose release 1.6.2](https://developer.android.com/jetpack/androidx/releases/wear-compose) and the [Material 2.5 to Material 3 migration guide](https://developer.android.com/training/wearables/compose/migrate-to-material3).
3. **Exact component defaults and generated token values:** the pinned AndroidX source under [`wear/compose/compose-material3` at commit `f65727cc5cc63d05724c0edb55900bc8790b14e8`](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/), associated with 1.6.2 rather than the moving `androidx-main`.
4. **Visual oracle:** screenshots from a small reference Wear Compose app using exactly 1.6.2, rendered at the same logical size and density.
5. **Embedded implementation constraints:** pinned [LVGL 9.5](https://docs.lvgl.io/9.5/introduction/repo.html), [Espressif's LVGL port](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.8.0~1/readme), ESP-IDF documentation, and measured behavior on target hardware.

### Fidelity labels

Every component and behavior gets one label in `docs/fidelity-matrix.md`:

- **Exact:** anatomy, metrics, state behavior, color roles, and motion match the oracle within the agreed screenshot threshold.
- **Equivalent:** same semantic job and interaction, adapted for square geometry or input limitations.
- **Inspired:** custom platform component using M3E tokens and grammar without an official counterpart.
- **Deferred:** deliberately absent with a reason and fallback.

Never use “exact” as a subjective judgment. It must have:

- oracle screenshots for required states;
- a token/source reference;
- an automated visual diff;
- a hardware interaction test when behavior is involved.

### Legal and attribution

- AndroidX source is Apache-2.0. Preserve notices for any adapted code or generated constants that are copyrightable.
- [Material Color Utilities](https://github.com/material-foundation/material-color-utilities) is Apache-2.0.
- [Material Symbols](https://github.com/google/material-design-icons) is Apache-2.0.
- [Roboto Flex](https://github.com/googlefonts/roboto-flex) fonts use SIL Open Font License 1.1.
- LVGL is MIT licensed.

Keep `THIRD_PARTY_NOTICES.md`, record exact upstream revisions, and avoid public naming that implies Google endorsement.

---

## 3. Deliverables

The implementation is complete only when the repository contains all of the following:

1. `m3e_lvgl`, a reusable host-side component library.
2. A token synchronization and code-generation pipeline pinned to an AndroidX release.
3. A font pipeline producing subsetted static Roboto Flex instances.
4. An icon pipeline producing a named, curated Material Symbols corpus.
5. An SDL component catalog running the exact production UI sources.
6. A Wear Compose reference app that renders the visual oracle.
7. A versioned AppSpec schema, JSON validator, CBOR codec, capability manifest, and device validator.
8. A keyed UI reconciler and one-way state/action runtime.
9. The Home, Voice, Calories, Calculator, and Workout reference apps.
10. Golden screenshot, interaction replay, fuzz, performance, memory, and hardware tests.
11. Two board support packages or adapters: CoreS3 SE and T-Watch S3.
12. A performance report from real T-Watch hardware.
13. Documentation for designers, generated-app authors, firmware developers, and server integration.

---

## 4. Repository shape

Use a monorepo so the schema, token pins, golden images, simulator, firmware, reference app, and server compiler evolve atomically.

```text
/
├── CMakeLists.txt
├── idf_component.yml
├── sdkconfig.defaults
├── THIRD_PARTY_NOTICES.md
├── docs/
│   ├── architecture.md
│   ├── architecture-decisions.md
│   ├── fidelity-matrix.md
│   ├── performance-budgets.md
│   ├── generated-app-authoring.md
│   ├── theme-authoring.md
│   └── release-process.md
├── firmware/
│   ├── main/
│   ├── boards/
│   │   ├── t_watch_s3/
│   │   ├── m5stack_cores3_se/
│   │   └── simulator/
│   ├── services/
│   │   ├── app_runtime/
│   │   ├── audio/
│   │   ├── download/
│   │   ├── storage/
│   │   ├── theme/
│   │   └── telemetry/
│   └── partitions/
├── components/
│   └── m3e_lvgl/
│       ├── include/m3e/
│       │   ├── foundation/
│       │   ├── layout/
│       │   ├── components/
│       │   ├── runtime/
│       │   └── platform/
│       ├── src/
│       │   ├── foundation/
│       │   ├── layout/
│       │   ├── components/
│       │   ├── runtime/
│       │   └── platform/
│       ├── generated/
│       │   ├── tokens/
│       │   ├── fonts/
│       │   └── icons/
│       └── test/
├── appspec/
│   ├── schema/
│   ├── examples/
│   ├── generated/
│   └── compatibility/
├── guest-sdk/
│   ├── rust/
│   └── examples/
├── simulator/
│   ├── src/
│   ├── profiles/
│   └── fixtures/
├── catalog/
│   ├── stories/
│   └── interaction_traces/
├── reference/
│   └── android-wear/
├── tools/
│   ├── token_sync/
│   ├── theme_compile/
│   ├── font_pipeline/
│   ├── icon_pipeline/
│   ├── screenshot_diff/
│   ├── appspec_compile/
│   └── hil_runner/
└── tests/
    ├── unit/
    ├── golden/
    │   ├── oracle/
    │   ├── lvgl_rgb888/
    │   ├── lvgl_rgb565/
    │   └── diffs/
    ├── integration/
    ├── fuzz/
    ├── performance/
    └── hardware/
```

Do not place generated files beside hand-authored source without a generated-file banner and deterministic regeneration command.

---

## 5. Runtime architecture

```text
 voice / touch / sensors / network
                 │
                 ▼
        platform event router
                 │
      ┌──────────┴───────────┐
      │                      │
      ▼                      ▼
 system UI state       guest app mailbox
                              │
                         WAMR / Rust Wasm
                              │
                       host capability API
                              │
        state patches + semantic actions
                              │
                              ▼
                  AppSpec runtime / store
                              │
                  validate → diff → batch
                              │
                              ▼
                     keyed UI reconciler
                              │
                              ▼
             m3e_lvgl components + theme
                              │
                              ▼
                      LVGL UI task only
                              │
                              ▼
                  BSP flush / touch / haptic
```

### Thread ownership

Use a strict ownership model:

- The LVGL/UI task owns every LVGL object and active animation.
- Audio, network, storage, sensor, download, and WAMR tasks communicate through bounded queues.
- A queue message carries immutable data, a reference-counted buffer, or a copied small value. It never carries an LVGL pointer.
- UI work is coalesced and applied once per frame.
- Any exceptional host call that must touch LVGL acquires `lvgl_port_lock()` and executes on the UI task; prefer posting a message instead.
- Display flush completion calls `lv_display_flush_ready()` only after SPI DMA has finished reading the buffer.
- Queue overflow policy is explicit: coalesce replaceable state, preserve user actions, and fail safely rather than block audio capture.

### C++ host layer

Wrap LVGL narrowly. Do not create a second widget framework.

```cpp
namespace m3e {

class Component {
 public:
  virtual ~Component();
  virtual lv_obj_t* object() const = 0;
  virtual void apply(const NodeProps& next, const UpdateContext&) = 0;
  virtual void set_theme(const ThemeContext&) = 0;
  virtual SizeDp measure(const ConstraintsDp&) = 0;
};

class UiRuntime {
 public:
  Result mount(const AppSpec&);
  Result patch(const StatePatch&);
  void dispatch(const UiEvent&);
  void set_theme(std::shared_ptr<const ResolvedTheme>);
  CapabilityManifest capabilities() const;
};

}
```

RAII wrappers may remove event callbacks, observers, animations, and owned LVGL objects. They must not hide object lifetime or permit cross-thread calls.

---

## 6. Token architecture

### 6.1 Canonical theme model

Separate a small user-facing recipe from the fully resolved device theme.

```json
{
  "schemaVersion": 1,
  "themeId": "violet-night",
  "seed": "#8B5CF6",
  "mode": "dark",
  "contrastLevel": 0.25,
  "variant": "expressive",
  "motion": "expressive",
  "shapeFamily": "rounded",
  "fontScale": 1.0
}
```

The server or build host resolves that recipe with Material Color Utilities into:

```text
ResolvedTheme
├── metadata: schema, id, generator version, CRC, source seed
├── color: all semantic Wear color roles in RGB888 and RGB565
├── typography: role → font id, px metrics, tracking, line height
├── shape: role → corner family and dimensions
├── spacing: role → dp
├── motion: role → duration, delay, easing/spring, reduced variant
├── state: pressed/focused/disabled/dragged opacity and overlays
└── haptic: semantic event → device waveform id and timing
```

Keep authoring colors in 24-bit form, but validate the final pairs after RGB565 quantization. A palette that passes in RGB888 can lose contrast when reduced to 5/6/5 bits.

### 6.2 Color roles

Mirror the pinned Wear `ColorScheme` API exactly in the canonical source model. At minimum support the primary, secondary, tertiary, error, background, and layered-surface families; all corresponding `on*` content colors; outlines; and dim/container variants present in Wear Material 3 1.6.2.

The stable API has 29 roles:

| Family | Roles |
|---|---|
| Primary | `primary`, `primaryDim`, `primaryContainer`, `onPrimary`, `onPrimaryContainer` |
| Secondary | `secondary`, `secondaryDim`, `secondaryContainer`, `onSecondary`, `onSecondaryContainer` |
| Tertiary | `tertiary`, `tertiaryDim`, `tertiaryContainer`, `onTertiary`, `onTertiaryContainer` |
| Surface | `surfaceContainerLow`, `surfaceContainer`, `surfaceContainerHigh`, `onSurface`, `onSurfaceVariant` |
| Outline | `outline`, `outlineVariant` |
| Background | `background`, `onBackground` |
| Error | `error`, `errorDim`, `errorContainer`, `onError`, `onErrorContainer` |

Use the token generator as source of truth. As an extraction audit, the pinned baseline dark tokens map approximately as follows:

| Role kind | Baseline tone |
|---|---:|
| background / on-background | Neutral 0 / 100 |
| surface low / default / high | Neutral 15 / 20 / 30 |
| on-surface / on-surface-variant | Neutral 95 / Neutral Variant 80 |
| outline / outline-variant | Neutral Variant 60 / 40 |
| accent main / dim / container | accent palette 90 / 80 / 30 |
| accent on-main / on-container | accent palette 10 / 95 |
| error main / dim / container | error palette 80 / 70 / 30 |
| error on-main / on-container | error palette 10 / 95 |

Do not guess the definitive field list in handwritten C++. Generate it from the pinned upstream API and check the generated artifact into source control. The generated output should resemble:

```cpp
struct WearColorScheme {
  Color primary;
  Color primary_dim;
  Color on_primary;
  Color primary_container;
  Color on_primary_container;
  // ...generated exact 1.6.2 roles...
  Color background;
  Color on_background;
  Color surface_container;
  Color surface_container_low;
  Color on_surface;
  Color on_surface_variant;
  Color outline;
  Color outline_variant;
  Color error;
  Color on_error;
  Color error_container;
  Color on_error_container;
};
```

Rules:

- Components request semantic roles; they never store literal application colors.
- Content/container pairings are encoded in the component default, not chosen ad hoc by app code.
- Black remains the dominant screen substrate for OLED-like Wear character, even though this LCD does not gain OLED power savings.
- Prefer tonal hierarchy over shadows. Shadows are both visually less Wear-native and expensive in software.
- No blur in the component contract.
- Gradients require an explicit component exception and hardware measurements. Solid tonal surfaces are the default.
- System safety, permissions, destructive actions, and errors use protected semantic roles that app themes cannot remap into ambiguity.
- Theme compilation fails if required contrast or role completeness checks fail.

### 6.3 Theme precedence

Resolve tokens in this order:

```text
firmware safety constraints
        >
system/accessibility override
        >
user global theme
        >
app semantic theme preference
        >
component variant
        >
local state (pressed, disabled, selected)
```

Generated apps may select roles or sanctioned variants. They may not supply arbitrary raw colors by default. An advanced signed first-party capability can permit restricted custom colors after contrast validation.

### 6.4 Live theme swapping

LVGL applies a theme to newly created objects; that is not sufficient for a live global recolor. Implement a shared style registry:

- one stable `lv_style_t` per component part/variant/state combination;
- component objects reference shared styles rather than copying raw color properties locally;
- a resolved theme update mutates those shared styles;
- invoke the appropriate LVGL style-change notification/refresh and invalidate affected roots;
- update all tokens in a staging registry, then atomically swap at a frame boundary;
- retain the old theme until the frame is complete;
- never reconstruct the whole component tree to change themes.

Acceptance target: a full theme swap completes in under 100ms, has no frame with mixed old/new roles, leaks no styles, and does not change app state or scroll position.

### 6.5 Typography

Expose the full Wear typography vocabulary even if multiple roles initially alias one bitmap:

- Display: small, medium, large.
- Title: small, medium, large.
- Label: small, medium, large.
- Body: extra small, small, medium, large.
- Numeral: extra small, small, medium, large, extra large.
- Arc: small, medium, large.

The following table is an audit target for the pinned stable tokens. Exact tracking, prominent-weight variants, and any source nuance must still come from `TypographyTokens.kt`.

| Role | Size / line height | Weight | Roboto Flex width axis |
|---|---:|---:|---:|
| Display large | 40 / 44sp | 500 | 110 |
| Display medium | 30 / 34sp | 520 | 110 |
| Display small | 24 / 26sp | 550 | 110 |
| Title large | 18 / 20sp | 500 | 110 |
| Title medium | 16 / 18sp | 550 | 110 |
| Title small | 14 / 16sp | 550 | 110 |
| Label large | 20 / 22sp | about 500 | 110 |
| Label medium | 15 / 18sp | about 500 | 110 |
| Label small | 13 / 16sp | about 500 | 110 |
| Body large | 16 / 18sp | 450 | 110 |
| Body medium | 14 / 16sp | 450 | 110 |
| Body small | 12 / 14sp | 500 | 110 |
| Body extra small | 10 / 12sp | 500 | 104 |
| Numeral extra large | 60 / 60sp | 560 | 110 |
| Numeral large | 50 / 50sp | 580 | 110 |
| Numeral medium | 40 / 40sp | 580 | 100 |
| Numeral small | 30 / 30sp | 550 | 100 |
| Numeral extra small | 24 / 24sp | 550 | 100 |
| Arc large | 18 / 22sp | about 599 | 100 |
| Arc medium | 15 / 18sp | about 599 | 100 |
| Arc small | 14 / 16sp | 560 | 100 |

Display and Numeral roles do not follow general user font scaling in the upstream guidance. Title, Body, Arc, and supported Label roles do, with caps for larger text. Preserve that distinction in the semantic API and test large-text layouts.

Generate the exact role names and upstream metrics from the pinned release. The initial physical asset set can be smaller:

- general regular and medium/semibold instances at the sizes actually used by P0 components;
- digit-only tabular instances for large numerals, timers, calorie totals, calculator results, and workout counters;
- icon-compatible baseline metrics;
- a locale fallback strategy.

Font pipeline:

1. Pin Roboto Flex source revision and license.
2. Use `fonttools` at build time to instantiate selected width, weight, grade, and optical-size axes.
3. Subset by declared locale/codepoint manifest.
4. Generate LVGL A4 bitmap fonts with `lv_font_conv`.
5. Generate digit-only fonts independently so large numerals do not pull in a full alphabet.
6. Emit ascent, descent, line-height, baseline, and tabular-width metadata.
7. Golden-test all roles at RGB565.
8. Report flash cost per font in CI.

Initial candidate raster sizes are 12, 14, 16, 20, 30/32, 40, and 48 physical pixels, but this is not a mandate. Derive the actual set from the 1.6.2 oracle at 1.25 density and merge only visually equivalent instances.

Do not use TinyTTF, FreeType, or runtime variable-font axes for the production English UI until profiling proves the cost acceptable. LVGL notes that compressed fonts render more slowly; only compress a font when flash savings justify the measured render penalty.

Animated numeral/text behavior should use fixed assets:

- crossfade;
- vertical digit slide;
- small scale/opacity emphasis;
- optional width interpolation between prebuilt instances.

It should not require interpolating a variable font on device.

### 6.6 Shape

Implement the upstream shape scale, initially:

| Role | Reference radius |
|---|---:|
| none | 0dp |
| extra small | 4dp |
| small | 8dp |
| medium | 18dp |
| large | 26dp |
| extra large | 36dp |
| full | pill/circle |

The token sync step must confirm exact 1.6.2 defaults and component mappings.

Shape support has three implementation tiers:

- **Tier A:** LVGL radius, width, height, circle, and pill interpolation. This covers most buttons, cards, chips, indicators, and expressive pressed states.
- **Tier B:** a custom-drawn fixed topology with independently parameterized corners or a cached mask. Use only where screenshot comparison shows a meaningful gap.
- **Tier C:** arbitrary path-to-path morphing. Deferred unless a specific flagship interaction justifies its CPU/RAM cost.

Cache geometry and masks by `(shape_id, width, height, scale)` and bound the cache. Never allocate a mask in an animation callback.

### 6.7 Motion

Define a `MotionScheme` with named semantic transitions rather than durations embedded in widgets:

```cpp
enum class MotionToken {
  state_fast,
  state_default,
  spatial_fast,
  spatial_default,
  spatial_slow,
  emphasized_enter,
  emphasized_exit,
  container_transform,
  list_item_transform,
  dialog_enter,
  dialog_exit,
  voice_pulse,
};
```

Each token resolves to duration, delay, easing or damped-spring coefficients, and a reduced-motion behavior.

Wear's stable `MotionScheme` exposes six core slots: fast/default/slow × spatial/effects. Use these pinned-source values as initial audit targets:

| Slot | Expressive spatial | Standard spatial | Effects |
|---|---|---|---|
| Fast | stiffness 800, damping ratio 0.70 | stiffness 1400, critically damped | stiffness 1400, non-bouncy |
| Default | stiffness 350, damping ratio 0.75 | stiffness 500, critically damped | stiffness 500, non-bouncy |
| Slow | stiffness 200, damping ratio 0.80 | stiffness 260, critically damped | stiffness 260, non-bouncy |

Spatial motion may overshoot; color and opacity effects must not. Preserve that distinction even if both are ultimately evaluated by the same scheduler.

Implementation:

- use `lv_anim_t` for integer geometry, opacity, radius, color, and transform properties;
- use LVGL animation paths for standard curves;
- implement one bounded fixed-step damped-spring evaluator or generate lookup tables offline for expressive springs;
- centralize scheduling, cancellation, re-targeting, and animation budget enforcement;
- update related properties in a single timeline so shape, color, haptic, and content changes remain synchronized;
- have reduced motion replace large spatial movement with short crossfades/state changes;
- prohibit heap allocation and logging in per-frame callbacks.

The motion system must be interruptible. Rapid taps or voice-state changes re-target from the current rendered value rather than snapping back to a stale start value.

### 6.8 State and haptic tokens

Components need common state semantics:

- enabled, disabled;
- pressed;
- focused/rotary-focused;
- selected/checked;
- dragged;
- loading;
- error;
- pending/optimistic;
- destructive confirmation.

Define haptics by meaning, not motor waveform:

```text
selection_tick
step_increment
step_limit
action_commit
action_reject
success
warning
error
voice_start
voice_stop
build_complete
```

The T-Watch BSP maps those to DRV2605 effects and timing. The CoreS3 development profile may use audio/log/visual substitutes. Haptics are dispatched from the same semantic timeline as the visual state and have their own rate limiter.

---

## 7. Rendering and memory strategy

### 7.1 Baseline display configuration

Use RGB565, partial render mode, and two internal-SRAM DMA-capable strip buffers.

Initial candidates:

| Board/profile | Strip | One buffer | Two buffers |
|---|---:|---:|---:|
| T-Watch S3 | 240×24 | 11,520B | 23,040B |
| T-Watch S3 | 240×40 | 19,200B | 38,400B |
| T-Watch S3 | 240×60 | 28,800B | 57,600B |
| CoreS3 | 320×40 | 25,600B | 51,200B |

Start with 40 rows. It is one sixth of the T-Watch screen and matches the current LilyGo LVGL helper's grounded choice. Benchmark 24, 40, and 60 rows; do not choose solely from theory.

Alternative experiment:

- full 240×240 RGB565 PSRAM canvas: 115,200B;
- one 20–40-row internal SRAM transfer buffer;
- `buff_spiram=true`, `buff_dma=false`, with the port's transfer staging.

This can simplify some transformations, but PSRAM rendering competes for cache and is not automatically faster. Keep it behind a measured board profile.

### 7.2 Bandwidth reality

A 240×240 RGB565 frame is 115,200 bytes.

- At theoretical 40MHz SPI payload rate: 23.04ms per full frame before commands, gaps, and rendering.
- At theoretical 80MHz: 11.52ms before overhead.
- A 30fps full-screen animation moves 3.456MB/s of pixel payload.

Therefore:

- target 30 fps for full-screen transitions and transformed scrolling;
- target 60 fps only for local animation with a small dirty region;
- invalidate the smallest correct region;
- do not continuously animate the entire voice screen just because it is visually attractive;
- offer 40/60/80MHz board profiles and stress-test 80MHz for corruption, temperature, sustained transfer, and low battery.

### 7.3 Memory placement

Keep in internal DMA-capable SRAM:

- display strips and SPI descriptors;
- current-frame draw state;
- timing-critical small widget state;
- UI queues;
- hot stacks.

Keep in flash or PSRAM:

- immutable fonts and icons, where the selected LVGL access path remains efficient;
- decoded AppSpec buffers and histories;
- large app data;
- non-hot image assets;
- WAMR linear memory;
- caches that tolerate eviction.

PSRAM shares cache with flash and large sequential access can evict instructions. Suspend or freeze affected UI/app work around flash-critical OTA/install operations where the IDF memory mode requires it.

### 7.4 Drawing rules

- Prefer LVGL rectangles, arcs, lines, labels, images/masks, and direct draw descriptors.
- Build the voice orb, expressive rings, and unusual progress silhouettes as bounded custom widgets.
- Avoid full-screen canvases.
- A 100×100 ARGB8888 local canvas is already 40KB; a 240×240 one is 230,400B.
- Allow at most one bounded temporary alpha canvas and declare its maximum dimensions in configuration.
- Avoid shadows and blur. Use tonal surfaces, borders, scale, and opacity for depth.
- Avoid large translucent layers; composite only the region that needs them.
- Cache precomputed arc geometry and shape masks.
- Use `LV_COLOR_FORMAT_RGB565_SWAPPED` if panel byte order otherwise forces a full software byte-swap pass.

---

## 8. Component system

Every component implementation must include:

- semantic purpose and appropriate/inappropriate use;
- anatomy and LVGL object tree;
- variants and sizes;
- all interactive and async states;
- exact token dependencies;
- layout/measurement behavior at 192dp and 225dp;
- touch and optional rotary behavior;
- accessibility label, value, role, state, and action metadata;
- animation and haptic timeline;
- object count, retained RAM, dirty-region expectation, and asset dependencies;
- catalog stories and oracle/golden tests;
- AppSpec representation and capability version.

### 8.1 P0: platform-critical foundation

Implement these before generated apps:

| Component | Required scope | LVGL approach |
|---|---|---|
| `MaterialTheme` | color, typography, shape, motion, state, haptic context | shared style/token registry |
| `Text` | all semantic roles, max lines, ellipsis, alignment, tabular numerals | `lv_label` wrapper |
| `AnimatedText` | numeral slide, crossfade, small emphasis | paired labels/custom clipping |
| `Icon` | named subset, tint, size roles, fallback | A8 image/mask or symbol font |
| `AppScaffold` | system overlay host, time policy, route/overlay coordination | screen root + overlay layers |
| `ScreenScaffold` | content insets, time/scroll coordination, bottom action slot | flex/grid containers |
| `TimeText` | clock, optional leading status, scroll-away | label + scaffold controller |
| `Button` | filled, tonal, outlined, child; icon/label/secondary label | styled object tree |
| `CompactButton` | compact action without violating touch target | visual child inside 48dp hit object |
| `IconButton` | sizes, filled/tonal/outlined, shape state | styled hit object + icon |
| `IconToggleButton` | checked state and morph | shared button primitive |
| `TextButton` | sizes and shape state | shared button primitive |
| `TextToggleButton` | checked state | shared button primitive |
| `ButtonGroup` | expressive width redistribution under press/focus | custom layout + coordinated animation |
| `Card` | app/title/standard/non-clickable variants | container + slots |
| `ListHeader` | title/header spacing and alignment | label container |
| `ListSubHeader` | grouped-list semantics | label container |
| `CircularProgressIndicator` | determinate/indeterminate | `lv_arc`/custom draw |
| `SegmentedCircularProgressIndicator` | segments, gaps, semantics | custom multi-arc draw |
| `LinearProgressIndicator` | determinate/indeterminate | `lv_bar` wrapper |
| `Slider` | steps, icon slots, value semantics | `lv_slider` + custom visuals |
| `Stepper` | increment/decrement, limits, long press policy | two actions + value |
| `CheckboxButton` | whole-row selection | button + glyph |
| `RadioButton` | whole-row exclusive selection | button + glyph |
| `SwitchButton` | whole-row binary selection | button + switch glyph |
| `AlertDialog` | title, icon, scrolling body, confirm/dismiss | modal overlay + scaffold |
| `ConfirmationDialog` | success/failure/generic confirmation | timed modal + animation |
| `Picker` | one-column wheel, selected item emphasis | snapped scroll list |
| `PickerGroup` | coordinated multi-column wheel | picker composition |
| `DatePicker` | locale-aware day/month/year composition | picker group |
| `TimePicker` | 12/24-hour modes | picker group |
| `HorizontalPager` | touch paging and keyed pages | tileview/custom pager |
| `HorizontalPagerScaffold` | time and page-indicator coordination | scaffold controller |
| `HorizontalPageIndicator` | selected page and overflow | custom dots/pills |
| `SwipeToDismissBox` | route dismissal gesture | gesture controller + transform |
| `ScrollIndicator` | list scroll position | custom edge indicator |

`ButtonGroup` deserves exact treatment because it is a signature expressive interaction:

- expose at most three buttons in the standard group;
- use 4dp inter-button spacing;
- start with horizontal content padding around 5.2% of screen width, confirmed against the pinned component token;
- a pressed button grows by 24dp in the reference behavior;
- immediate neighbors give up the same total width so group width remains constant;
- a pressed middle item splits compensation across both neighbors;
- use fast spatial motion on press and slow spatial motion on release;
- preserve the underlying 48dp touch targets even as visual widths move;
- cancel/re-target from current geometry during rapid pointer changes;
- under reduced motion, use a smaller non-overshooting redistribution or a state-layer change.

### 8.2 P1: expressive depth

Implement after the P0 vertical slice is solid:

| Component/behavior | Fidelity goal | Notes |
|---|---|---|
| `TransformingLazyColumn` | exact/equivalent | Most important expressive behavior after core components. Virtualize items and derive scale/height/opacity from distance to focal line. |
| `SurfaceTransformation` | exact/equivalent | Apply compatible transform to cards/buttons without per-item canvas layers. |
| transformed-height measurement | exact | Layout height must reflect transformation to prevent gaps and snapping. |
| list snapping | exact/equivalent | Velocity-aware settling and anchor preservation. |
| `AnimatedPage` | exact/equivalent | coordinated scale/scrim/entry/exit; one full-screen transition at a time |
| `FadingExpandingLabel` | exact/equivalent | bounded line expansion with no stale-text flash |
| `SwipeToReveal` | equivalent | one/two semantic actions, partial/full thresholds, haptic ticks |
| split checkbox/radio/switch buttons | exact/equivalent | primary row action and independent selection affordance |
| `LevelIndicator` | equivalent | edge/linear adaptation for square display |
| vertical pager and indicator | equivalent | only if a product flow needs them |
| loading placeholders | inspired by M3 behavior | skeleton/tonal pulse with reduced-motion variant |
| shape morph states | exact where feasible | circle↔pill and compatible corner transitions |
| expressive spring motion | perceptually matched | derive from oracle traces; bounded evaluator |

#### Transforming list algorithm

Do not scale a bitmap of each row. Recompute semantic layout parameters.

For each visible item:

```text
d = normalized signed distance from item's visual center to the viewport focal line
w = clamp(1 - abs(d), 0, 1)
scale = lerp(edge_scale, 1.0, easing(w))
opacity = lerp(edge_opacity, 1.0, easing(w))
height = base_height * height_transform(scale, component_kind)
shape = interpolate(edge_shape, focal_shape, w)
```

Requirements:

- virtualize; instantiate visible items plus a small prefetch window only;
- cache measured base heights;
- compute in fixed point where practical;
- update only when scroll position changes;
- preserve the anchor item and offset during data updates;
- clip once at the list viewport rather than adding deep clips per row;
- do not animate more properties than visually matter;
- support a reduced-motion mode that reduces scale change and disables spring overshoot;
- test slow drag, fast fling, interrupted fling, item insertion/removal, theme swap mid-scroll, and app state patch mid-scroll.

### 8.3 P2: deferred or hardware-dependent

| Official item | Initial decision |
|---|---|
| `EdgeButton` | Defer on square hardware. Keep API slot reserved and implement if a future round profile is funded. Use a full-width bottom action on square. |
| `curvedText` / arc typography | Expose typography roles but defer rendering on square. Implement only with a round-display milestone. |
| `OpenOnPhoneDialog` | Replace with a platform-specific `ContinueElsewhereDialog` for ChatGPT/server/phone handoff, or defer until handoff exists. |
| round-only edge geometry | Simulator/reference only initially. |
| 1.7 alpha one-handed gesture APIs | Exclude from the 1.6.2 baseline. Track in an experimental branch and reconsider after stable release. |
| arbitrary vector shape morphing | Defer; compatible-parameter morphing covers the product need. |

This is the permitted “drop one or two things” boundary. Do not use it as permission to omit scaffolds, transformed lists, state behavior, typography roles, or the component variants needed by generated apps.

### 8.4 Calculator-specific optimization

Use LVGL's button matrix for the calculator keypad. A button matrix uses virtual buttons with much lower per-key overhead than a separate object and label for every key. Wrap it in a semantic `Keypad` component so:

- each key has role/action/accessibility metadata;
- operators, digits, and destructive keys receive semantic variants;
- hit targets remain correct;
- pressed-state animation can be drawn per virtual cell;
- keyboard layout is versioned in AppSpec;
- calculation logic remains in Rust/Wasm or a tested host service, not inside the widget.

---

## 9. Custom components for this product

These are not official Material components. Mark them **Inspired**, build them from the same primitives/tokens, and make them first-class catalog items.

### 9.1 VoiceOrb

The orb is system-owned, globally available, and visually continuous across app boundaries.

State model:

```text
dormant
idle
arming
listening
speech_detected
transcribing
thinking
clarifying
previewing_action
building
downloading
installing
success
warning
error
cancelling
offline
```

The orb should not be a video or a stack of full-screen alpha canvases. Build it as a custom LVGL widget with a small parameter model:

```cpp
struct VoiceOrbVisualState {
  q8_8 radius;
  q8_8 inner_radius;
  q8_8 amplitude;
  q8_8 phase;
  q8_8 halo_opacity;
  ColorRole core;
  ColorRole halo;
  uint8_t segment_count;
  VoiceGlyph glyph;
};
```

Draw from circles, arcs, small lobes, and opacity. Update only the orb's dirty rectangle. Audio amplitude should be sampled/rate-limited to the visual frame rate and smoothed; never feed microphone callback cadence into LVGL.

Motion rules:

- dormant/idle: static or an extremely low-duty subtle pulse;
- listening: local deformation or ring activity bounded to the orb;
- thinking: controlled rotation/phase motion, not a full-screen loop;
- success/error: short semantic transition plus haptic;
- reduced motion: color, icon, and short opacity changes only;
- low battery/idle: no continuous animation.

Interaction:

- tap toggles capture or opens voice;
- long press may provide push-to-talk;
- a visible cancel action is always available once capture has started;
- the system owns the semantics and cannot be impersonated by downloaded apps.

### 9.2 VoiceSheet / VoiceOverlay

Host-owned system layer containing:

- orb;
- live transcript;
- status text;
- clarification choices;
- action/data preview;
- cancel/confirm controls;
- build and download progress;
- error and retry state.

It needs a state machine rather than scattered booleans:

```text
Idle
  → Capturing
  → Transcribing
  → Reasoning
  → Clarifying | Previewing | Building
  → Downloading
  → Installing
  → Committing
  → Success | Error
  → Idle
```

On entry, save route, scroll anchor, focused semantic node, and input mode. Suspend app interaction without destroying it. On exit, restore focus/state unless the action intentionally navigated elsewhere.

### 9.3 Transcript

- partial and final segments are visually distinct;
- final text does not jump when partial text is replaced;
- cap visible history and store the rest outside the UI tree;
- redact sensitive content from remote logs;
- support an `aria-live`-like semantic update policy without repeating the whole transcript;
- long text scrolls or expands within a bounded area rather than resizing the orb indefinitely.

### 9.4 ClarificationChoiceGroup

Use a maximum of three high-quality choices plus cancel/rephrase. It may reuse `ButtonGroup` when choices are short, or a modal list when labels are longer. Voice and touch must select the same semantic action IDs.

### 9.5 ChangeReview

All consequential voice edits should have a structured preview:

```text
entity / record
field
old value
new value
scope
side effects
confirm / edit / cancel
```

Examples:

- calorie entry before insertion;
- prior meal correction;
- deletion;
- workout set adjustment;
- new app permissions;
- global theme change.

Minor reversible actions may commit optimistically and offer Undo. Permissions, destructive changes, broad shared-data changes, and app installation require explicit review.

### 9.6 BuildProgress

Represent real stages rather than a fake indeterminate spinner:

```text
planning
generating UI
generating logic
compiling
running tests
rendering previews
packaging
downloading
verifying
installing
ready
```

Each stage is an event from the server/install pipeline. If exact progress is unavailable, show the current stage without invented percentages. The UI remains cancellable until the atomic activation boundary.

### 9.7 PermissionReview

Explain capabilities in human terms and map each to a signed bundle manifest entry:

```text
Read nutrition history
Add and edit nutrition entries
Read body weight
Use network through named integration “USDA Foods”
Send notifications
```

Never display a vague “access shared database” permission. Scope by namespace and operation.

### 9.8 LiveCard / Glance

Home cards are constrained system-rendered projections:

- app icon and name;
- primary value/status;
- optional secondary value;
- one tap target;
- optional small progress/indicator;
- freshness timestamp;
- no app-owned nested scroll or arbitrary animation.

An installed app supplies a `GlanceSpec`, not an unrestricted UI subtree. The host renders it using a fixed `LiveCard` component and can quarantine a broken or stale app without destabilizing Home.

### 9.9 Platform status components

Add:

- connectivity/server state;
- sync pending/failed;
- battery/charging;
- download/install;
- offline mode;
- app crash/recovery;
- storage pressure.

Keep these available to the host but expose only safe read-only values to apps.

---

## 10. AppSpec: the generated UI contract

### 10.1 Principles

- AppSpec describes meaning and composition, not LVGL implementation.
- JSON is the authoring/debug representation.
- Canonical CBOR is the signed transport and device representation.
- One source schema generates C++ types, Rust SDK types, TypeScript/server types, JSON Schema, CBOR tags, validators, and documentation.
- Stable node IDs make tree reconciliation possible.
- Generated apps select semantic variants and tokens, not arbitrary styling.
- The device validates again even if the server already validated.

Do not use LVGL XML as the app distribution format. Do not ship native component code in an app bundle.

### 10.2 Base node

```json
{
  "id": "current_weight",
  "type": "stepper",
  "visible": { "bind": "session.active" },
  "enabled": true,
  "props": {
    "label": "Weight",
    "value": { "bind": "session.current.weight" },
    "unit": "lb",
    "step": 5,
    "tone": "primary",
    "size": "large"
  },
  "events": {
    "valueCommitted": "set_weight"
  },
  "semantics": {
    "label": "Current set weight",
    "value": { "format": "{value} pounds" }
  },
  "testTag": "workout.current_weight"
}
```

Shared fields:

```text
id: stable NodeId
type: versioned ComponentKind
visible: bool or bounded predicate
enabled: bool or binding
props: component-specific typed object
events: semantic event → ActionId
semantics: label/value/hint/state/sensitivity overrides
testTag: optional stable testing name
```

### 10.3 Layout vocabulary

P0:

```text
Screen
Column
Row
Box
Grid
Spacer
ScrollableColumn
TransformingList
HorizontalPager
OverlaySlot
```

P1:

```text
VerticalPager
FlowRow
StickyHeaderList
```

Rules:

- use spacing tokens such as `none`, `xs`, `sm`, `md`, `lg`, not pixel numbers;
- absolute positioning is reserved for host-owned system components;
- only one primary scroll axis per screen;
- nested scrolling requires an explicitly sanctioned component;
- grid columns and keypad layouts use bounded templates;
- app content cannot draw in system insets;
- visible content cannot create a hit target smaller than policy permits;
- children and depth are bounded before any LVGL object is allocated.

### 10.4 Component properties

Expose semantic choices:

```text
tone: primary | secondary | tertiary | neutral | error
emphasis: high | medium | low
size: compact | default | large
shape: component_default | compact | prominent
alignment: start | center | end
progress_style: linear | circular | segmented
```

Do not initially expose:

- raw colors;
- arbitrary corner radii;
- font family, point size, or weight;
- arbitrary animation curves/durations;
- arbitrary z-index;
- arbitrary transforms;
- arbitrary image URLs;
- raw haptic effects;
- panel coordinates;
- LVGL flags, parts, states, or widget names.

If a real application need cannot be expressed, evolve the semantic schema or create a reusable component. Do not add a generic “escape hatch” that bypasses the platform.

### 10.5 Bindings

```text
Binding<T> =
    Literal<T>
  | StatePath<T>
  | StatePath<T> + FormatSpec
```

Initial value types:

- Boolean;
- signed integer;
- fixed-point decimal;
- string;
- enum;
- timestamp and duration;
- semantic color/tone role;
- small typed record;
- bounded typed list.

Supported host-safe formatting/predicates:

```text
number/unit/date/time/duration formatting
equals / not_equals
less_than / greater_than
all / any / not
exists
plural selection from explicit strings
```

Do not embed a general expression language. Business calculations and derived application state live in Rust/Wasm.

### 10.6 Events

Translate LVGL input to a small semantic event set:

```text
tap
longPress
repeat
valueChanging
valueCommitted
checkedChanged
pageChanged
dismissed
revealed
scrollEnded
submit
retry
cancel
```

Events name an `ActionId`; they never name a guest export or native function.

Event envelope:

```json
{
  "schema": 1,
  "appId": "calories",
  "screenId": "today",
  "nodeId": "quick_add",
  "actionId": "open_quick_add",
  "kind": "tap",
  "timestampMonotonicMs": 817391,
  "payload": {}
}
```

### 10.7 State namespaces

```text
screen.*    ephemeral view/session state
app.*       private persistent state
shared.*    user-owned cross-app entities
system.*    read-only time/battery/connectivity/preferences
session.*   transient app process/session state
```

Access to `shared.*` requires a manifest permission. A shared database does not mean every app can read or mutate every record.

Example:

```json
{
  "statePermissions": [
    {"namespace": "shared.nutrition", "access": "read_write"},
    {"namespace": "shared.body_weight", "access": "read"}
  ]
}
```

Voice edits use the same typed transaction layer:

```text
parse intent
→ resolve entity and fields
→ permission check
→ validate schema
→ show review if needed
→ atomic transaction
→ audit entry
→ state notification
```

Never let voice or the server execute ad hoc SQL on the watch.

### 10.8 Reconciliation

Do not rebuild a screen for every guest response.

```cpp
struct ViewHandle {
  NodeId id;
  ComponentKind kind;
  lv_obj_t* root;
  uint64_t props_hash;
  ScreenArena* arena;
  SemanticNode* semantics;
  SmallVector<ViewHandle*, 4> children;
};
```

Patch operations:

```text
SetProperty
SetBinding
InsertChild
RemoveChild
MoveChild
ReplaceSubtree
ShowRoute
DismissRoute
ShowOverlay
DismissOverlay
```

Rules:

- validate the complete patch transaction before applying anything;
- same ID and component kind updates in place;
- kind changes replace only that subtree;
- preserve list/pager/selection state when keys remain;
- coalesce replaceable high-frequency patches;
- batch updates once per UI tick, then perform layout/invalidation;
- use a screen arena for view metadata and bindings;
- destroy all observers, callbacks, and animations before releasing the arena;
- retain no pointer into a decoded network buffer or guest linear memory.

LVGL's observer/subject system can be useful behind an internal `BindingHub`, but AppSpec and Wasm must not depend on LVGL subjects.

### 10.9 Initial hard quotas

These are security boundaries as well as performance boundaries. Calibrate them after the hardware spike, then version them in the capability manifest.

| Resource | Target | Hard initial ceiling |
|---|---:|---:|
| LVGL objects per screen | ≤150 | 250 |
| AppSpec tree depth | ≤8 | 12 |
| children per generic container | ≤16 | 32 |
| simultaneous visibly moving objects | ≤3 | 4 |
| simultaneously animated properties | ≤8 | 12 |
| full-screen spatial transitions | 1 | 1 |
| local ARGB temporary canvases | 0 | 1 |
| local ARGB canvas memory | 0 | 40KB |
| arbitrary path morphs | 0 | 0 |
| unbounded shadows/blur/Lottie | 0 | 0 |
| primary scroll axes | 1 | 1 |
| decoded image dimensions/bytes | manifest-defined | device capability |
| patch/message size | tune from fixtures | fail before allocation |
| strings/list lengths | schema-specific | fail before allocation |

Also bound:

- observers/subscriptions;
- route depth;
- timers;
- update rate;
- persistent storage;
- network response size;
- WAMR stack, heap, linear memory, and response buffer;
- icons, fonts, and image asset count.

An app that exceeds a target but remains under the hard ceiling gets a server linter warning. An app over a hard ceiling cannot install or render.

### 10.10 Capability negotiation

The firmware advertises exact support:

```json
{
  "runtimeApi": "1.4",
  "appspec": "1.3",
  "hostAbi": 1,
  "componentSetHash": "sha256:...",
  "displayProfile": "watch_square_192",
  "colorFormat": "rgb565",
  "limits": {
    "nodesPerScreen": 250,
    "animationProperties": 12
  },
  "features": [
    "m3e.transforming_list.v1",
    "m3e.animated_text.v1",
    "system.voice_capture.v1",
    "shared_state.v1"
  ]
}
```

The server compiles to that manifest. Unknown required components, variants, actions, or permissions reject installation. Optional decorative features may have explicitly defined fallbacks.

---

## 11. Wasm and host capability boundary

Keep the ABI small and message-oriented.

Suggested guest exports:

```text
abi_version() -> u32
alloc(length: u32) -> u32
dealloc(pointer: u32, length: u32)
init(pointer: u32, length: u32) -> packed_slice_u64
handle_event(pointer: u32, length: u32) -> packed_slice_u64
suspend(pointer: u32, length: u32) -> packed_slice_u64
resume(pointer: u32, length: u32) -> packed_slice_u64
```

Inputs and outputs are canonical CBOR. A packed slice contains an offset and length in guest linear memory.

Host call procedure:

1. Validate input envelope and guest lifecycle state.
2. Allocate a bounded guest buffer.
3. Copy the event into guest memory.
4. Invoke the export on the app actor task, never the UI task.
5. Validate returned address and length.
6. Copy the result immediately into host-owned memory.
7. Release guest memory.
8. Decode and validate `CommandBatch`.
9. Check every command against manifest capabilities.
10. Post approved state/UI/service work to bounded subsystem queues.

Never retain a native pointer into Wasm memory. Never call back into LVGL reentrantly from Wasm.

Host capabilities should be semantic and narrow:

```text
state.read
state.transact
timer.schedule
timer.cancel
navigation.command
ui.patch
integration.request
notification.schedule
log.bounded
```

An integration call names a declared integration and operation; it does not grant arbitrary sockets or secrets.

Guest controls:

- serialized actor execution;
- memory and stack quotas;
- maximum command/response count and byte size;
- execution-time telemetry;
- trap/crash counter;
- health timeout;
- cancellation;
- quarantine after repeated failure.

Measure WAMR interpreter and AOT modes. Keep Wasm as the canonical build artifact; target-specific AOT can be produced by the server later if it produces a worthwhile measured gain. Do not make the first UI milestone depend on AOT.

---

## 12. Navigation, layers, and system ownership

Use a host navigator:

```text
Push(route, params)
Pop
Replace(route, params)
Reset(route)
ShowDialog(spec)
DismissDialog
ShowToast(message)
```

Apps request commands; the host owns the actual screen stack.

Layer model:

```text
system layer   voice, permission, install, crash/recovery, critical alerts
top layer      app dialogs, confirmations, toasts
active screen  current app/navigation scene
```

The system layer cannot be drawn or intercepted by an app.

Memory policy:

- retain current and prior screen trees only when interactive swipe-back needs both;
- keep deeper history as route/state records;
- reconstruct deeper screens on return;
- cap route depth;
- abort a transition cleanly if the destination cannot mount within quota;
- always keep a minimal recovery/Home surface available outside the guest runtime.

---

## 13. Semantic tree and accessibility

Build a semantic tree parallel to the LVGL object tree:

```text
role
label
value
state
hint
available actions
focus/read order
logical and physical bounds
sensitive-data classification
owner (system/app)
```

This is valuable even before a full screen reader:

- voice can resolve “tap Log food” or “increase weight”;
- Codex can reason about the current screen from a privacy-filtered description;
- simulator tests can inspect meaning instead of pixels;
- crown/buttons can navigate focus order;
- future accessibility support has a real foundation.

Rules:

- every interactive node needs a useful label;
- labels for visible sibling actions must be distinguishable;
- hidden nodes are not actionable;
- hit target is normally at least 48×48dp, with 40×40dp only for an approved compact exception;
- icons may remain 24dp inside a larger transparent hit target;
- visual and touch bounds are separate;
- list items are at least 32dp high;
- large display/numeral roles may remain fixed-size, while supported body/label roles respect bounded user scaling;
- touch order and voice order follow semantic order, not LVGL creation order;
- sensitive values are omitted or redacted from server context without explicit permission.

System preferences to support from the beginning:

```text
reduced_motion
high_contrast
large_body_text
left_handed_input
haptics_enabled
```

---

## 14. Initial screen designs

These five experiences are not demo work after the framework. They are the acceptance suite that shapes it.

### 14.1 Home

Purpose: show time, the most useful live state, and an immediate path into voice or apps.

Composition:

```text
AppScaffold
├── TimeText / compact status
├── HorizontalPagerScaffold
│   ├── LiveCard: current workout / next action
│   ├── LiveCard: calories and protein
│   ├── LiveCard: app-defined glance
│   └── app launcher card/grid if needed
├── HorizontalPageIndicator
└── VoiceOrb, system anchored
```

Behavior:

- first page is chosen by recency/context, but layout remains stable;
- each installed app contributes at most one constrained `GlanceSpec`;
- glances have freshness metadata and a safe fallback;
- long press or a dedicated affordance opens theme/settings;
- the orb remains reachable across pages;
- a broken or hung app cannot block the clock, voice, or navigation.

Acceptance:

- paging meets the full-screen 30fps target;
- time/page indicator/scroll-away behavior is coherent;
- voice opens within one frame of input feedback;
- Home remains usable with network, server, database, or guest failures;
- theme changes preserve live card state;
- no guest code runs merely to paint each frame.

### 14.2 Voice

Composition by state:

```text
idle:          orb + short prompt
listening:     animated orb + partial transcript + cancel
thinking:      orb + final transcript + phase label
clarifying:    transcript + choice group + rephrase/cancel
previewing:    ChangeReview + confirm/edit/cancel
building:      BuildProgress + current stage + cancel
installing:    verified progress + app identity
success/error: semantic confirmation + next action
```

Key rules:

- no fake percentages;
- critical data edits and permissions get preview;
- visually distinguish “heard,” “interpreted,” and “committed”;
- voice can be dismissed without losing the underlying screen;
- offline commands that can be executed locally should remain available;
- server latency does not block animations or input;
- audio amplitude is sampled and smoothed outside the UI task.

### 14.3 Calorie tracker

Primary screen:

```text
ScreenScaffold
├── TimeText
├── Numeral: calories remaining/consumed
├── SegmentedCircularProgressIndicator or CircularProgressIndicator
├── compact macro row/cards: protein, carbs, fat
├── quick-add Button / bottom full-width action
└── TransformingList: meal groups and entries
```

Secondary flows:

- quick add calories/macros;
- food search/integration result;
- structured voice preview;
- day/history;
- edit/delete with undo or confirmation;
- goals/settings.

Data model should separate food, serving, entry, meal group, daily target, and computed daily summary. Store source/provenance and allow offline manual entries.

Acceptance:

- totals update via bindings without reconstructing the list;
- voice can add and correct entries through typed transactions;
- changes are reversible/audited;
- theme swap preserves scroll anchor and dialog state;
- calorie and macro numerals use tabular glyphs;
- local core flow works offline;
- remote food lookup is clearly an optional integration.

### 14.4 Calculator

Composition:

```text
ScreenScaffold
├── expression, small body/label
├── result, large tabular Numeral + AnimatedText
├── Keypad, button-matrix backed
└── optional history affordance
```

Rules:

- calculation state is local and deterministic;
- operators/digits/destructive controls use semantic tones;
- `=` is the primary action;
- result fitting has deterministic thresholds;
- repeated rapid taps are queued/coalesced correctly without missing semantic events;
- keypad objects are not recreated when the expression changes.

Acceptance:

- ten taps per second lose no input;
- pressed feedback appears within 50ms p95;
- no network/server dependency;
- large-result updates remain within local-animation budget;
- accessible labels distinguish operators;
- theme changes do not reduce keypad hierarchy or contrast.

### 14.5 Weight workout tracker

Active set screen:

```text
ScreenScaffold
├── exercise title / set count
├── large weight Numeral + Stepper
├── large reps Numeral + Stepper
├── primary Complete Set button
├── prior/next set summary
└── optional exercise TransformingList
```

Rest screen:

```text
large tabular countdown
SegmentedCircularProgressIndicator
add/subtract time controls
skip / next set
```

Rules:

- workout, exercise, set plan, performed set, and rest timer are distinct records;
- completing a set and starting rest is one atomic transaction;
- rest uses monotonic time and can reconstruct its remaining duration after screen sleep;
- voice “log 255 for five” resolves against current exercise/context and previews the typed action;
- completion and timer haptics are semantic;
- active session restores safely after reboot/crash.

Acceptance:

- entire active workout works offline;
- rapid stepper repeat is responsive and rate-limited;
- a guest crash cannot lose an already committed set;
- screen sleep does not drift the timer;
- state survives app upgrade/rollback;
- the active session can be controlled by touch or voice using the same action IDs.

---

## 15. Server build, test, install, and hot-load pipeline

### 15.1 Bundle

```text
app.bundle
├── manifest.cbor
├── ui.appspec.cbor
├── guest.wasm or guest.aot
├── state-schema.cbor
├── migrations/
├── assets/
├── source-map/debug metadata (development only)
└── signature
```

Manifest:

```json
{
  "bundleFormat": 1,
  "appId": "com.example.calories",
  "version": "0.3.0",
  "appspec": {"major": 1, "minor": 3},
  "hostAbi": 1,
  "minRuntime": "0.6.0",
  "maxTestedRuntime": "0.8.x",
  "requiredComponents": {
    "m3e.metric_card": 1,
    "m3e.circular_progress": 1,
    "m3e.button": 2
  },
  "capabilities": [
    "shared.nutrition.read_write",
    "voice.structured_capture"
  ],
  "dataSchema": 3,
  "resourceEstimate": {
    "screenNodesMax": 96,
    "wasmMemoryBytes": 262144,
    "assetBytes": 48120
  }
}
```

### 15.2 Server pipeline

1. Fetch the exact device capability manifest.
2. Have Codex generate/modify app manifest, AppSpec, state schema, Rust logic, tests, and migration.
3. Validate schemas and permissions.
4. Run generated-app design lints.
5. Compile Rust to Wasm.
6. Run Rust unit tests and host/Wasm contract tests.
7. Render every route in the exact LVGL SDL simulator build.
8. Execute scripted interactions.
9. Render default, vivid, muted, monochrome, high-contrast, and app-requested themes.
10. Render 192dp square, 225dp reference, and long-text/locale fixtures.
11. Check semantics, touch targets, contrast after RGB565 quantization, truncation, tree/resource quotas, and unsupported features.
12. Compare app screen goldens if the app already exists.
13. Produce performance/resource estimates.
14. Package, hash, and sign the bundle.
15. Stream progress using real named stages.

### 15.3 On-device preflight

1. Download to a staging slot/file.
2. Verify length, hash, signature, publisher/trust policy.
3. Parse bounded manifest.
4. Check runtime/AppSpec/ABI/component versions.
5. Check permissions and resource limits.
6. Verify every asset and CBOR section before activation.
7. Dry-run data migrations against a copy or transaction.
8. Instantiate the guest under quota.
9. Validate its initial command response.
10. Mount the first route off the active display or in a test context.
11. Atomically switch the app registry pointer/version.
12. Retain the prior version and data backup through a health window.
13. Roll back automatically on init, migration, mount, or early health failure.

“Hot reload” should mean atomic app replacement while the trusted host remains running—not arbitrary native code injection.

### 15.4 Versioning

Version independently:

```text
bundle format
AppSpec schema
CBOR wire schema
component API
Wasm host ABI
theme format
application data schema
```

Rules:

- numeric CBOR tags never change meaning;
- optional additive fields advance minor versions;
- breaking semantics advance major versions;
- unknown optional decoration may be ignored only when the schema says so;
- unknown component kinds, required variants, or event semantics reject installation;
- transform a bounded set of old AppSpec minors to the current internal AST;
- data migration is explicit, versioned, testable, transactional, and rollback-aware;
- quarantine a new app version after repeated activation failure.

---

## 16. Performance engineering plan

The numbers below are project gates to calibrate on real hardware, not claims that have already been measured.

### 16.1 Product budgets

| Metric | Initial gate |
|---|---|
| Full-screen/transformed motion | ≥30 fps; p95 frame ≤33.3ms; no repeated frame >50ms |
| Local orb/button/progress motion | ≥55 fps; p95 ≤18.2ms when dirty area ≤30% |
| Touch to visible pressed state | p95 <50ms |
| Haptic dispatch after semantic threshold | <20ms |
| UI CPU when idle | ≤5% of one core; strive for lower |
| UI CPU in typical transition | p95 ≤70% of one core |
| Idle redraw | none unless clock/status changes |
| Active moving objects | target ≤3 |
| Active animation properties | target ≤8 |
| Screen mount from cached spec | target <150ms |
| Color-only theme application | target <100ms |
| Internal heap, normal fully loaded system | ≥128KiB free and ≥64KiB largest block |
| Internal heap, stress floor | ≥64KiB free and ≥32KiB largest block |
| Generated screen objects | target ≤150, hard ceiling 250 |
| Local ARGB canvas | ≤40KB and at most one |

Measure heap gates with Wi-Fi, streaming JSON, audio capture/playback, WAMR, database, and UI all initialized. Measuring an isolated UI demo is insufficient.

### 16.2 First executable benchmark

Before building dozens of components, create `tests/performance/ui_stress_scene`:

- full-screen color/tonal fill;
- large tabular numeral changing every frame;
- 20 representative buttons/cards;
- three arcs and one segmented indicator;
- four-button group press/release;
- ten-item transforming list under slow drag and fast fling;
- dialog over active content;
- orb local animation;
- full-screen page transition;
- color-only theme swap;
- shape/typography theme swap;
- concurrent simulated network events, audio sampling, WAMR events, and database notifications.

Benchmark matrix:

```text
board: CoreS3 SE, T-Watch S3
buffer rows: 24, 40, 60
buffer count: 1, 2
buffer placement: internal partial, PSRAM canvas + SRAM transfer
SPI clock: 40, 60, 80MHz where supported
refresh period: 10, 16, 20, 33ms
LVGL draw units: 1 and 2 if supported/configured
font compression: selected off/on
profile build: profiler enabled, release optimization
```

Capture:

```text
render start/end
flush start/end
dirty region and pixel count
frame deadline miss
input arrival and painted response
active animation count
UI task CPU and stack high-water
internal free/minimum/largest block
PSRAM free/minimum/largest block
WAMR memory
audio underruns
network queue depth
flash/component sizes
display checksum/corruption evidence
```

Output a machine-readable JSON report plus a human Markdown summary. Commit the chosen board configuration and raw baseline.

### 16.3 T-Watch candidate default

```text
panel: ST7789V3, 240×240
format: RGB565 or RGB565_SWAPPED after byte-order test
render mode: partial
SPI: 80MHz candidate, with validated 40/60 fallbacks
DMA queue depth: 2
buffer_size: 240 * 40 pixels in esp_lvgl_port config
two actual buffers: 19,200 bytes each
memory: internal MALLOC_CAP_DMA, correctly aligned
flush_ready: display transfer completion callback only
```

Note that `lvgl_port_display_cfg_t.buffer_size` is expressed in pixels, not bytes.

Stress acceptance:

- 30 minutes of repeated full-screen transfer at the selected SPI clock;
- no corruption, tearing outside accepted mechanism, deadlock, touch starvation, or memory drift;
- include low-battery and sustained-warm conditions where practical.

### 16.4 CoreS3 candidate default

```text
panel viewport: centered 240×240 product viewport on 320×240 physical panel
format: RGB565
buffer_size: 320 * 40 pixels
two actual buffers: 25,600 bytes each
driver: official ESP BSP/esp_lcd path preferred
```

An M5Unified/M5GFX flush bridge is acceptable for the earliest firmware milestone, but keep the component/render code independent of that bridge and converge on an `esp_lcd`-compatible BSP path for parity.

### 16.5 Power and idle

- Use the LVGL port's sleep/wake integration.
- Stop animation timers when no semantic motion is active.
- Redraw clock/status only when values change.
- Freeze the orb when dormant.
- Use touch interrupt/wake behavior supported by the actual board.
- Test T-Watch touch sleep carefully: current board documentation/source notes that the FT6336U reset pin is not connected, so a naïve touch sleep may not recover normally.
- Pause guest apps when the display is off unless they have a declared background timer/service capability.
- Use monotonic host timers for workout/rest and reconstruct display state on wake.
- Measure current draw per product state: idle face, Home, active scroll, voice capture, Wi-Fi stream, build/download, app runtime, display off.

### 16.6 Adaptive degradation

If real hardware misses a budget, degrade in this order:

1. reduce nonessential concurrent motion;
2. shorten/replace spatial motion with opacity;
3. reduce transformed-list scale/radius updates;
4. lower local animation target from 60 to 30fps;
5. switch custom masks to simpler rounded rectangles;
6. remove decorative gradients/alpha;
7. use plain list fallback for low-memory mode;
8. disable nonessential animation under concurrent voice/audio load.

Do not degrade touch feedback, legibility, semantic state, error clarity, or input responsiveness.

---

## 17. Reference renderer and visual fidelity

### 17.1 Pin the oracle

Use:

```text
Maven artifact: androidx.wear.compose:compose-material3:1.6.2
AndroidX source commit: f65727cc5cc63d05724c0edb55900bc8790b14e8
```

Pin links:

- [stable source root](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/main/java/androidx/wear/compose/material3/)
- [generated token sources](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/main/java/androidx/wear/compose/material3/tokens/)
- [official samples](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/samples/src/main/java/androidx/wear/compose/material3/samples/)
- [AndroidX screenshot tests](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/androidTest/kotlin/androidx/wear/compose/material3/)

Track 1.7 alpha changes in `docs/upstream-notes/1.7-alpha.md`; do not let them enter the v1 API or goldens until stable and deliberately adopted.

### 17.2 Android reference app

Create `reference/android-wear`:

- dependency pinned to 1.6.2;
- a deterministic route/story per component, variant, and state;
- no system clock/randomness in captures;
- fixed fonts and locale;
- round upstream reference profile;
- 240×240px at 200dpi square comparison profile where Android tooling permits, representing 192dp at 1.25 density;
- instrumentation to report measured bounds, padding, text baseline, resolved color roles, shape, and animation keyframes;
- ADB capture script with deterministic filenames.

The square reference is a comparison aid, not proof that upstream supports square. Approved square adaptations need their own LVGL goldens and design review.

### 17.3 Token synchronization

Create `tools/token_sync`:

1. Verify the expected AndroidX commit hash.
2. Read the 1.6.2 global and per-component token files.
3. Extract identifiers, dimensions, color-role mappings, typography-role mappings, shape-role mappings, state values, and motion references.
4. Normalize into `reference/material-tokens/material_wear_1_6_2.json`.
5. Merge a small reviewed metadata/deviation file where mechanical extraction cannot infer semantics.
6. Generate C++ enums/default tables, TypeScript/Rust types, catalog metadata, and documentation.
7. Fail CI if regeneration differs.
8. Include origin file/line metadata in generated comments where practical.

Do not hand-copy hundreds of constants into component source. Per-component token mappings are as important as global theme roles.

### 17.4 Exact baseline token facts

The generator remains normative, but these are useful audit checks:

- the stable Wear color API has 29 roles;
- accent families include main, dim, container, on-main, and on-container roles;
- surface hierarchy includes low/default/high containers;
- typography has 21 roles;
- shape radii include 4/8/18/26/36dp plus none/full;
- `MotionScheme` has fast/default/slow spatial and effects slots;
- `ButtonGroup` is normally capped at three buttons and redistributes width under press;
- Wear M3 removed the old vignette; do not implement it.

For an initial baseline dark scheme, the pinned token source maps background to neutral tone 0; surface layers to neutral 15/20/30; accent main/dim/container to approximately tones 90/80/30; and corresponding on-colors to dark/light counterpart tones. Generate and validate these values rather than treating this paragraph as source code.

### 17.5 Screenshot comparison

Render the same story in:

- Android reference/oracle;
- LVGL SDL RGB888 if useful for diagnosis;
- LVGL SDL RGB565;
- hardware capture when possible.

Comparison outputs:

- exact pixel diff for deterministic LVGL-to-LVGL regression;
- perceptual diff for Compose-to-LVGL comparison;
- geometry overlay;
- baseline/bounds report;
- heatmap `_diff.png`;
- explicit masks only for known nondeterministic content.

Define per-story tolerances. Font rasterizers differ, so do not hide geometry errors behind a very loose whole-screen perceptual threshold. Test separately:

- token equality before quantization;
- quantized contrast;
- component bounds;
- padding;
- baseline/alignment;
- final perceptual image;
- motion keyframes.

Human approval is required to establish or update an approved square adaptation golden.

---

## 18. Testing and CI

### 18.1 Component catalog

Build an LVGL SDL “Storybook” using the exact production source:

- 240×240 product viewport;
- 320×240 CoreS3 view;
- 192dp and 225dp profiles;
- every variant and state;
- minimum, normal, maximum, and translated text;
- missing data/icon/error;
- default, vivid, muted, monochrome, high-contrast, and several RGB565-hostile seed colors;
- reduced motion;
- debug overlays for logical/physical/touch bounds, semantic IDs, focus order, token names, dirty regions, object count, active animation count, FPS, render/flush time, and memory.

Each story declares expected:

- screenshot;
- semantic tree;
- event trace;
- haptic trace;
- object count/resource class;
- supported profiles.

### 18.2 Unit tests

- fixed-point dp→px conversion and rounding distribution;
- color role completeness;
- RGB888→RGB565 conversion;
- contrast after quantization;
- theme merge/precedence;
- shape interpolation;
- motion curves/spring evaluator;
- layout and transformed-list formulas;
- token generation;
- schema validators;
- canonical CBOR round trips;
- navigation;
- state transactions/revisions;
- binding formatting;
- reconciler patch behavior;
- capability/version matching;
- bundle verification.

### 18.3 Deterministic motion tests

Inject a fake clock. Capture 0%, 25%, 50%, 75%, and 100% keyframes plus interruption/re-target scenarios. Test reduced-motion output separately.

### 18.4 Interaction tests

- rapid and slow press;
- long press/repeat;
- disabled controls;
- toggle/select state;
- ButtonGroup redistribution;
- stepper boundaries;
- slider commit versus changing events;
- picker snap;
- pager indicator;
- transformed-list slow drag/fast fling/interruption;
- swipe reveal/dismiss thresholds;
- dialog modality;
- voice open/cancel/clarify/commit/error;
- focus restoration;
- theme swap mid-interaction;
- app termination while dialog/animation is active;
- screen sleep/wake during a timer;
- app update and rollback during safe lifecycle states.

Assert semantic event stream, state changes, haptics, navigation, semantics, final screenshot, and memory return.

### 18.5 Semantic/static rules

Fail a generated screen if:

- an interactive node lacks a useful label;
- labels are ambiguous within scope;
- focus order is invalid;
- target is too small;
- visible text/container contrast fails after RGB565;
- hidden content remains actionable;
- app attempts to impersonate system UI;
- sensitive state would be sent remotely without permission;
- more than one primary scroll axis exists;
- unsupported raw styling or a prohibited effect appears;
- a destructive action lacks confirmation/undo policy.

### 18.6 Wasm and parser tests

Fixture guests:

- valid initialization and event response;
- oversized response;
- invalid CBOR;
- invalid pointer/length;
- trap;
- long-running/hung call;
- unauthorized state/integration request;
- unsupported navigation;
- resource quota exceedance;
- data migration success/failure/rollback.

Fuzz on desktop with sanitizers:

- AppSpec JSON/CBOR;
- theme blob;
- state transaction;
- guest command response;
- bundle manifest;
- patch validator;
- asset manifest.

Random valid specs must render without assertion. Random invalid specs must fail before unsafe allocation or LVGL mutation.

### 18.7 Soak tests

- open/close every catalog story repeatedly;
- apply 1,000 color themes;
- alternate structural themes;
- navigate continuously;
- update/reorder maximum transforming lists;
- load/unload guests;
- install/activate/rollback bundles;
- simulate connectivity and server failure;
- run 24 hours with virtual time and repeated wake/sleep.

Track heap minimum, largest block, LVGL objects, observers, animations, queues, task stack, and file handles. Steady-state drift should be effectively zero.

### 18.8 CI lanes

Per commit:

```text
format/static analysis
schema and token regeneration check
host unit tests with ASan/UBSan
Rust SDK and guest fixtures
simulator interactions
RGB565 golden tests
short fuzz corpus
ESP-IDF debug build
ESP-IDF release build
idf.py size / size-components / size-files
license/provenance audit
```

Nightly or attached hardware:

```text
flash CoreS3 and T-Watch
run catalog/performance sequence
collect serial JSON and profiler trace
exercise touch/haptics/audio/network/WAMR contention
run app load/unload and theme loops
run five vertical slices
run selected power states
archive measurements and compare to baseline
```

Profile builds may enable LVGL sysmon, observer diagnostics, monkey/random input, and profiler/Perfetto traces. Production builds disable debug overhead.

---

## 19. Phased implementation plan

Every phase should land as a reviewable, working vertical increment. Do not begin the next ocean of components while the previous phase has no tests or hardware measurements.

### Phase 0 — Freeze reference and decisions

Tasks:

- pin AndroidX 1.6.2 and source commit;
- pin LVGL/port/IDF versions and dependency lock;
- create source/license ledger;
- generate official component inventory;
- establish fidelity labels;
- approve 192dp→240px model;
- document square adaptations and permitted omissions;
- create 1.7 tracking document.

Deliverables:

- `M3_BASELINE.md`;
- `component-matrix.yaml`;
- `fidelity-matrix.md`;
- `architecture-decisions.md`;
- `THIRD_PARTY_NOTICES.md`.

Exit gate:

- every upstream component is assigned a tier and disposition;
- exact sources are reproducible;
- “faithful” is measurable.

### Phase 1 — Display/BSP and performance spike

Tasks:

- create CoreS3 and T-Watch display profiles;
- implement async RGB565 flush and touch adapters;
- implement configurable strip buffers/SPI clocks;
- create stress scene and telemetry;
- benchmark internal partial versus PSRAM canvas;
- validate DMA completion and port locking;
- test touch wake/sleep behavior;
- select release baseline.

Exit gate:

- raw reports exist for both boards;
- display does not corrupt in stress test;
- selected configuration meets or has an approved path toward frame/input budgets;
- memory/resource quotas receive their first measured values.

### Phase 2 — Simulator, reference app, and generation tools

Tasks:

- build SDL 240×240/320×240 simulator;
- build Android reference app;
- implement token sync;
- implement screenshot capture/diff;
- make clocks, data, and animation time deterministic;
- add catalog shell and debug overlays.

Exit gate:

- one reference Button story renders on Android and LVGL;
- automated diff artifacts are produced;
- CI deterministically regenerates tokens and goldens.

### Phase 3 — Foundation tokens and primitives

Tasks:

- fixed-point units/display profiles;
- generated color scheme and component tokens;
- theme validation and shared style registry;
- typography/font pipeline;
- icon pipeline;
- shapes/motion/state/haptic tokens;
- `Surface`, `TouchTarget`, `StateLayer`, `Text`, `Icon`;
- semantic node primitive.

Exit gate:

- live color theme swap is atomic;
- RGB565 contrast suite passes;
- fonts/icons have size reports;
- primitive catalog stories match approved oracle/adaptations;
- no component uses raw application colors.

### Phase 4 — Core components

Tasks:

- button family;
- toggle button family;
- ButtonGroup;
- card family;
- headers/labels;
- progress indicators;
- calculator Keypad;
- basic list/scroll;
- haptic and interaction state machinery.

Exit gate:

- all states have screenshots, semantic snapshots, event/haptic traces;
- ButtonGroup signature motion meets budget;
- calculator input stress loses no events;
- object/memory cost is recorded per component.

### Phase 5 — Scaffolds, navigation, paging, overlays

Tasks:

- AppScaffold/ScreenScaffold;
- TimeText/scroll-away;
- page/scroll indicators;
- horizontal pager/scaffold;
- AnimatedPage;
- navigation and route stack;
- dialog/confirmation overlay foundations;
- system/top/active layer ownership;
- swipe dismiss.

Exit gate:

- navigation/modal/input routing is deterministic;
- time and indicators coordinate correctly;
- one full-screen transition respects motion budget;
- recovery/Home layer exists independently of guests.

### Phase 6 — State, AppSpec, semantics, reconciliation

Tasks:

- authoritative schema and generators;
- JSON/CBOR parser/validators;
- state namespaces and transactions;
- BindingHub;
- keyed reconciler;
- semantic tree;
- capability manifest and resource accounting;
- simulator AppSpec viewer.

Exit gate:

- state changes update properties without full-screen rebuild;
- invalid patch transactions have no partial effect;
- screen teardown releases all observers/objects;
- fuzz corpus cannot reach LVGL with invalid input;
- semantic lints are enforced.

### Phase 7 — Wasm guest runtime

Tasks:

- Rust guest SDK;
- CBOR ABI;
- WAMR app actor task;
- capability calls;
- memory/time/response quotas;
- trap/hang/recovery;
- fixture apps;
- interpreter/AOT measurement.

Exit gate:

- guest never receives an LVGL pointer;
- bad addresses/responses fail safely;
- hung guest cannot freeze trusted UI;
- guest load/unload restores memory;
- first `display_text("Hello from Wasm")` milestone is reimplemented through the semantic runtime, not left as the final API.

### Phase 8 — Home and voice vertical slices

Tasks:

- LiveCard/Glance;
- Home pager;
- VoiceOrb;
- VoiceOverlay/Transcript/Clarification/ChangeReview;
- BuildProgress/PermissionReview;
- audio/server event integration;
- interruption/restoration tests.

Exit gate:

- Home and voice are excellent on both boards;
- system UI remains responsive with a hung guest and failing network;
- voice can interrupt/restore all existing screens;
- orb motion respects audio and power constraints.

### Phase 9 — Transforming list and form components

Tasks:

- virtualized TransformingLazyColumn;
- SurfaceTransformation;
- snapping and anchor preservation;
- checkbox/radio/switch and split variants;
- Slider/Stepper;
- Picker/PickerGroup/DatePicker/TimePicker;
- SwipeToReveal;
- LevelIndicator;
- remaining dialog variants;
- reduced-motion equivalents.

Exit gate:

- difficult components meet hardware budget or invoke an explicit adaptive fallback;
- no unexplained component-matrix gaps in P0/P1;
- inserting/updating list data does not snap or leak.

### Phase 10 — Calories vertical slice

Tasks:

- generated AppSpec and Rust/Wasm app;
- nutrition state schema and migration;
- binding-driven totals;
- manual entry/history/edit;
- structured voice transaction;
- optional food integration;
- per-app theme preference.

Exit gate:

- first full generated app builds, tests, installs, hot-activates, works offline, survives update, and rolls back;
- app looks first-party;
- state remains correct through theme, scroll, voice, and install transitions.

### Phase 11 — Calculator and workout vertical slices

Tasks:

- calculator guest and keypad;
- workout guest, rest timer, haptics, recovery;
- rapid-input and background timer tests;
- voice semantics;
- shared state permissions.

Exit gate:

- rapid calculator input and offline workout session pass on real hardware;
- timer uses monotonic time and survives sleep;
- committed workout data survives crash/update.

### Phase 12 — Theme authoring

Tasks:

- server-side Material Color Utilities integration;
- theme recipe/resolved theme schemas;
- preview and validation;
- global/per-app precedence;
- voice theme commands;
- persistence/rollback;
- optional short role interpolation after benchmark.

Exit gate:

- arbitrary accepted seeds produce complete, readable RGB565 schemes;
- failed theme cannot strand the UI;
- safety UI remains recognizable;
- live apply meets its budget.

### Phase 13 — Production generated-app pipeline

Tasks:

- Codex machine-readable component catalog;
- app templates and prompt context;
- server compiler/test renderer;
- bundle signing;
- on-device preflight;
- atomic activation and health window;
- rollback/quarantine;
- data migrations;
- progress streaming.

Exit gate:

- a voice prompt can generate, build, test, install, launch, and later modify a sample app without manual firmware changes;
- deliberately malformed or over-budget apps fail before activation.

### Phase 14 — Hardening and release

Tasks:

- 24-hour soak;
- power measurement;
- low battery/network/storage failure;
- localization and font packs;
- performance regression gates;
- recovery documentation;
- security review;
- accessibility audit;
- license audit.

Exit gate:

- all five vertical slices pass on T-Watch hardware;
- no meaningful resource drift;
- trusted Home/voice/recovery survive all guest failures;
- release artifacts and measurements are reproducible.

---

## 20. Codex working protocol

Use this section as direct instructions to the implementation agent.

### For every component

1. Read the pinned AndroidX component source, its generated token file, samples, API docs, and screenshot tests.
2. Add or update its entry in `component-matrix.yaml`.
3. Write anatomy/state/token/adaptation notes before code.
4. Add the Android oracle story.
5. Add the LVGL catalog story.
6. Implement using existing internal primitives first.
7. Add semantics, touch bounds, events, haptics, reduced motion, and theme dependencies.
8. Add unit, screenshot, interaction, and teardown tests.
9. Measure object count, retained memory, dirty pixels, and frame cost.
10. Record exact, equivalent, inspired, or deferred status.

Do not mark a component complete because its default static screenshot looks right.

### For every generated-app schema change

1. State the application need that cannot be expressed.
2. Prefer a semantic property/component over a styling escape hatch.
3. Update the authoritative schema.
4. Regenerate all language bindings and docs.
5. Add forward/backward compatibility tests.
6. Update firmware capability manifest.
7. Add valid/invalid examples and resource accounting.
8. Update Codex context/catalog.

### For every optimization

1. Preserve a benchmark case.
2. Record before/after frame, CPU, dirty pixels, internal RAM, PSRAM, and flash.
3. Verify visual and semantic goldens.
4. Check both boards.
5. Prefer deleting work over clever caches that create unbounded memory.

### Pull-request definition of done

- implementation and public API;
- generated artifacts up to date;
- catalog stories;
- RGB565 goldens;
- semantic snapshot;
- interaction and haptic trace;
- reduced-motion behavior;
- teardown/leak check;
- resource metrics;
- documentation and fidelity matrix;
- source/license provenance;
- hardware result when the feature affects animation, input, display, sleep, audio contention, or memory materially.

---

## 21. First 12 implementation issues

These are intentionally small enough to hand to Codex sequentially.

1. **Freeze dependencies and reference commit**  
   Add lockfiles, baseline docs, component matrix generator input, and license ledger.

2. **Create display-profile/fixed-point unit module**  
   Implement 192dp→240px conversion, consistent rounding, safe insets, and unit tests.

3. **Bring up LVGL SDL catalog shell**  
   Add 240×240 RGB565 and 320×240 profiles, deterministic clock, screenshot output, and debug overlay.

4. **Bring up both hardware display adapters**  
   Async flush, port lock, touch, DMA buffer profiles, and serial telemetry.

5. **Implement the hardware stress scene**  
   Run the complete buffer/SPI matrix and check in baseline report.

6. **Create Android 1.6.2 oracle app**  
   Button, Text, Card, progress, and scaffold starter stories with deterministic captures.

7. **Implement token sync and generated core theme types**  
   Include exact 29 color and 21 typography roles plus component token provenance.

8. **Implement theme compiler and RGB565 contrast audit**  
   Use Material Color Utilities on host/server; produce a signed/versioned resolved blob.

9. **Implement font/icon pipelines**  
   Static Roboto Flex instances, tabular numeral subsets, curated Material Symbols, size reports.

10. **Implement foundational primitives and Button family**  
    Shared styles, state layer, touch target, semantics, haptics, catalog/golden tests.

11. **Implement AppScaffold and system layer shell**  
    TimeText, active/top/system layers, deterministic navigation, placeholder VoiceOrb.

12. **Implement minimal AppSpec/reconciler/Wasm loop**  
    Replace the raw `display_text` proof with a validated semantic `Text` screen and action round trip.

After issue 12, reassess measurements and sequence the broader component phases. Do not spend months completing the component inventory before proving the generated-app loop.

---

## 22. Key risks and explicit mitigations

| Risk | Why it matters | Mitigation |
|---|---|---|
| “Material-like,” not faithful | Global colors/radii miss component-specific behavior | Pin exact source/tokens, Android oracle, per-state goldens, fidelity matrix |
| Square-screen mismatch | Upstream Wear is round-first | First-class display profile and approved square adaptations, never hidden approximations |
| Typography flash explosion | 21 roles × locales × variable axes | static instances, role aliasing only after visual test, digit subsets, locale packs, CI size reports |
| Transforming list cost/bugs | layout, scroll physics, clipping, focus, state updates interact | independent milestone, virtualization, fixed-point math, anchor tests, plain-list fallback |
| Theme update stalls/fragments | thousands of local styles would be expensive | shared style registry, staged atomic update, dependency map, bounded active theme contexts |
| SPI/display bandwidth | full screen is 115,200B per frame | 30fps full-screen target, partial invalidation, double DMA strips, measured SPI profiles |
| PSRAM assumed “free” | cache contention and DMA restrictions | hot/internal versus bulk/PSRAM policy, benchmark full canvas, heap gates with complete system running |
| Decorative effects consume RAM/fill rate | blur, shadow, ARGB, Lottie are software-heavy | tonal hierarchy, direct draw, one small canvas, explicit AppSpec prohibitions |
| Guest freezes UI | generated logic may hang or trap | actor task, queues, quotas, cancellation, trusted Home/recovery independent of guest |
| Generated UI denial of service | valid-but-huge trees/updates can exhaust resources | validate before allocation, hard quotas, rate limits, server lint, capability manifest |
| Cross-app data exposure | “shared DB” can erase app boundaries | typed namespaces, manifest permissions, transaction/audit layer, no raw SQL |
| Unsafe live install | partial update/migration can brick app/data | signed bundle, staging, preflight, transactional migration, atomic activation, rollback/quarantine |
| Voice ambiguity | spoken edits can be consequential | semantic tree, typed actions, preview/confirm/undo, protected system UI |
| Power regression | perpetual expressive animation drains watch | idle sleep, semantic timer stopping, static dormant orb, power-state tests |
| Touch fails after sleep | T-Watch FT6336U reset wiring caveat | board-specific wake experiment, avoid unsupported touch sleep, test PMU rail cycle |
| Upstream churn | 1.7 alpha changes semantics | stable 1.6.2 baseline, separate tracking doc, explicit upgrade process |
| Brand/license ambiguity | open-source code/assets and Material marks have terms | exact provenance, notices/licenses, descriptive non-endorsing product language, counsel before distribution |

---

## 23. What not to do

- Do not make every generated app a newly compiled LVGL firmware image.
- Do not expose a generic canvas to ordinary apps.
- Do not let apps provide CSS-like arbitrary style dictionaries.
- Do not create a separate `lv_obj_t` plus label for every calculator key.
- Do not use full-screen ARGB8888 buffers as the default.
- Do not keep animations running just to make an idle screen feel alive.
- Do not mutate LVGL from WAMR, network, audio, or storage tasks.
- Do not call flush-ready before DMA completion.
- Do not allocate/free in an animation callback.
- Do not rebuild the entire screen for one bound value change.
- Do not assume a palette remains accessible after RGB565 conversion.
- Do not claim Compose fidelity without an oracle and per-state comparison.
- Do not silently adopt a new AndroidX, LVGL, ESP-IDF, font, or icon revision.
- Do not let “shared database” become universal read/write permission.
- Do not let an app imitate the voice, permissions, install, or recovery layer.
- Do not fake build percentages.
- Do not make the future generated-app pipeline block the first hardware benchmark.

---

## 24. Definition of the first substantial release

The release is ready when:

- nearly every relevant stable Wear M3 Expressive component is implemented or explicitly square-adapted;
- omissions are limited to round-only, Android-companion-only, or measured-unaffordable features with good fallbacks;
- color, typography, shape, motion, state, and haptics form one coherent theme system;
- every exposed component has schema, docs, semantics, catalog stories, tests, and resource metrics;
- Home, Voice, Calories, Calculator, and Workout are excellent on a real T-Watch S3;
- generated apps cannot bypass theme, layout, accessibility, permissions, or resource limits;
- an app can be generated, server-built, tested, signed, downloaded, preflighted, atomically activated, modified, and rolled back;
- global and per-app themes can be changed by voice and applied safely;
- Wasm, server, app, theme, or integration failure cannot take down Home, voice, or recovery;
- motion looks recognizably expressive while meeting measured frame and power budgets;
- the semantic tree makes every app inherently voice-addressable;
- CI and device reports make visual, memory, speed, compatibility, and power regressions obvious.

The architecture should be summarized this way:

> Build a broad, native Material 3 Expressive component framework on LVGL, but expose a narrower, typed, semantic composition language to generated apps. Let the native framework boil the ocean; keep generated output constrained enough that it is beautiful, themeable, accessible, safe, and fast by construction.

---

## 25. Copy/paste kickoff prompt for Codex

```text
Implement this repository according to “Material 3 Expressive for Wear on LVGL:
Codex-ready implementation plan.”

Begin with Phase 0 and the first five implementation issues only. Do not start the
large component inventory yet.

Requirements:

1. Pin Wear Compose Material 3 to 1.6.2 and AndroidX source commit
   f65727cc5cc63d05724c0edb55900bc8790b14e8 as the reference oracle.
2. Pin LVGL 9.5.0 and esp_lvgl_port 2.8.0~1; commit dependency locks. Start with a
   pinned ESP-IDF 5.5.x patch and keep an IDF 6.0 compatibility lane separate.
3. Implement a 192×192dp logical watch viewport mapped to 240×240 physical pixels
   using fixed-point scaling. The CoreS3 must show that viewport centered in its
   320×240 panel.
4. Bring up the SDL simulator and both board display profiles in RGB565.
5. Implement async DMA flush correctly: display flush completes only from the
   transfer-completion callback. Keep all LVGL calls on the UI task/under the port
   lock.
6. Implement the configurable hardware stress scene and benchmark 24/40/60-row
   buffers, one/two buffers, supported 40/60/80MHz SPI clocks, and PSRAM-canvas
   staging versus internal partial rendering.
7. Emit machine-readable telemetry for render time, flush time, dirty pixels,
   deadline misses, touch-to-paint latency, CPU, task high-water marks, internal
   and PSRAM heap statistics, WAMR/audio contention, and binary size.
8. Do not claim performance that has not been measured on attached hardware.
9. Use C++17 without exceptions/RTTI for host UI types, Rust/Wasm for guest logic,
   and JSON-authoring/canonical-CBOR for AppSpec.
10. Preserve existing user changes and stop for any architecture conflict that
    would invalidate this plan.

For each issue, first inspect the current repository, state the files and interfaces
you will change, implement the smallest complete vertical increment, run all relevant
tests, and report results plus remaining risks. Add architecture decisions and
source/license provenance as you go.

The first gate is not “many components.” It is a deterministic simulator, correct
board flush/input paths, a reproducible Material reference pin, and measured display,
memory, and input behavior on real hardware.
```

---

## 26. Sources and further reference

### Material 3 Expressive for Wear

- [Wear Compose release history and current stable/alpha status](https://developer.android.com/jetpack/androidx/releases/wear-compose)
- [Stable 1.6.2 AndroidX source root at pinned commit](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/main/java/androidx/wear/compose/material3/)
- [Generated Material token source at pinned commit](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/main/java/androidx/wear/compose/material3/tokens/)
- [Pinned Material samples](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/samples/src/main/java/androidx/wear/compose/material3/samples/)
- [Pinned AndroidX screenshot tests](https://android.googlesource.com/platform/frameworks/support/+/f65727cc5cc63d05724c0edb55900bc8790b14e8/wear/compose/compose-material3/src/androidTest/kotlin/androidx/wear/compose/material3/)
- [Material 3 migration and component inventory](https://developer.android.com/training/wearables/compose/migrate-to-material3)
- [Wear Material 3 API index](https://developer.android.com/reference/kotlin/androidx/wear/compose/material3/package-summary)
- [Material 3 Expressive Wear design language](https://developer.android.com/design/ui/wear/guides/get-started/design-language)
- [Official Wear design kits](https://developer.android.com/design/ui/wear/guides/get-started/design-kits)
- [Wear adaptive design](https://developer.android.com/design/ui/wear/guides/foundations/adaptive-design)
- [Adaptive quality tiers and breakpoints](https://developer.android.com/design/ui/wear/guides/foundations/quality-tiers/adaptive-differentiated)
- [Wear color system](https://developer.android.com/design/ui/wear/guides/styles/color)
- [Wear color roles and tokens](https://developer.android.com/design/ui/wear/guides/styles/color/roles-tokens)
- [Wear typography roles](https://developer.android.com/design/ui/wear/guides/styles/typography/type-scale-tokens)
- [Wear accessibility](https://developer.android.com/training/wearables/accessibility)
- [Material Color Utilities](https://github.com/material-foundation/material-color-utilities)
- [Roboto Flex](https://github.com/googlefonts/roboto-flex)
- [Material Symbols](https://github.com/google/material-design-icons)

### LVGL

- [LVGL 9.5.0 release](https://github.com/lvgl/lvgl/releases/tag/v9.5.0)
- [LVGL 9.5 documentation](https://docs.lvgl.io/9.5/introduction/repo.html)
- [Display setup](https://docs.lvgl.io/9.5/main-modules/display/setup.html)
- [Color formats](https://docs.lvgl.io/9.5/main-modules/display/color_format.html)
- [Styles and themes](https://docs.lvgl.io/9.5/common-widget-features/styles/themes.html)
- [Style transitions](https://docs.lvgl.io/9.5/common-widget-features/styles/transitions.html)
- [Scrolling and snapping](https://docs.lvgl.io/9.5/common-widget-features/scrolling.html)
- [Button matrix](https://docs.lvgl.io/9.5/widgets/buttonmatrix.html)
- [Observer/data binding](https://docs.lvgl.io/9.5/main-modules/observer/observer.html)
- [Font overview](https://docs.lvgl.io/9.5/main-modules/fonts/overview.html)
- [Built-in/generated bitmap fonts](https://docs.lvgl.io/9.5/main-modules/fonts/built_in_fonts.html)
- [Canvas memory considerations](https://docs.lvgl.io/9.5/widgets/canvas.html)
- [Random-input monkey testing](https://docs.lvgl.io/9.5/debugging/monkey.html)
- [LVGL SDL integration](https://docs.lvgl.io/9.5/integration/pc/sdl.html)
- [LVGL profiler](https://docs.lvgl.io/9.5/debugging/profiler.html)

### ESP32-S3, ports, and boards

- [Espressif LVGL port 2.8.0~1](https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.8.0~1/readme)
- [Official LVGL ESP-IDF integration guide](https://docs.lvgl.io/9.5/integration/chip_vendors/espressif/add_lvgl_to_esp32_idf_project.html)
- [Espressif LVGL performance guidance](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md)
- [ESP32-S3 external RAM guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html)
- [ESP-IDF heap capabilities](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/mem_alloc.html)
- [SPI master/DMA guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/spi_master.html)
- [ESP32-S3 performance guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/performance/speed.html)
- [ESP32-S3 RAM measurement](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html)
- [T-Watch S3 official documentation](https://wiki.lilygo.cc/products/t-watch-series/t-watch-s3/)
- [Current LilyGo LVGL 9 helper](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/src/LV_Helper_v9.cpp)
- [Current LilyGo display bridge](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/src/LilyGoDispInterface.cpp)
- [Current LilyGo T-Watch board initialization](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/master/src/LilyGoWatchS3.cpp)
- [M5Stack CoreS3 SE documentation](https://docs.m5stack.com/en/core/M5CoreS3%20SE)
- [Espressif CoreS3 BSP](https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_core_s3)
- [M5Unified](https://github.com/m5stack/M5Unified)

### Wasm

- [WAMR exported host API and memory validation](https://bytecodealliance.github.io/wamr.dev/docs/html/wasm__export_8h.html)
- [WAMR execution modes](https://bytecodealliance.github.io/wamr.dev/blog/introduction-to-wamr-running-modes/)

---

## 27. Research note

This plan was built from current official design, AndroidX, LVGL, Espressif, LilyGO, M5Stack, Material asset, and WAMR references. The existing LilyGo source verifies that its current T-Watch path uses RGB565, an 80MHz SPI candidate, queue depth two, and two one-sixth-screen buffers; that makes the 40-row starting point board-grounded.

No hardware benchmark was run for this document because no board or firmware repository was attached to the working environment. The frame, CPU, heap, latency, and binary-size numbers above are therefore explicit initial engineering targets. Phase 1 makes real CoreS3 and T-Watch measurement the first executable gate, before component proliferation.
