# Dual-renderer conformance lab

Status: initial Compose oracle lane implemented

Stable oracle: Wear Compose Material 3 `1.6.2`
Product renderer: LVGL `9.5.0`, RGB565, 240×240 square

The complete dual-renderer implementation and progress tracker is
[`Project Parallax`](project-parallax.md).

## Contract

One renderer-neutral reference scenario supplies deterministic data, UI state,
theme selection, display-profile matrix, font scale, interaction phase and an
expected semantic tree:

```text
reference scenario
      ├── appspec/lifecycle links
      ├── deterministic semantic state
      └── expected accessibility nodes
                    |
          +---------+---------+
          |                   |
          v                   v
 Wear Compose 1.6.2      Doodad AppSpec/LVGL
          |                   |
          +---------+---------+
                    |
 screenshots / semantics / bounds / motion / resource evidence
```

`contracts/conformance-scenario-v1.schema.json` remains the lifecycle and
provider-event contract. It is not expanded with renderer details.
`contracts/reference-render-scenario-v1.schema.json` is the thin visual and
semantic envelope. A reference scenario may link to an AppSpec and lifecycle
scenario by project-root-relative path, but neither renderer owns the
authoritative domain state.

## Geometry policy

| Profile | Purpose | Geometry family |
|---|---|---|
| `watch_square_240` | Primary API 37 runtime and product comparison | `watch_square_240` |
| `wear_round_small` | Compact circular adaptation oracle | `wear_round` |
| `wear_round_large` | Adaptive breakpoint oracle | `wear_round` |

The profiles share semantic state, color, typography, shape and motion roles.
They do not share safe-area calculation, edge geometry, list transformation,
curved text, arc placement, outer margins or corner behavior.

The exact-geometry square API 37 screenshots are the primary app-screen
runtime oracle. Round screenshots answer how upstream Material adapts to
circular Wear displays. Approved square composition decisions remain product
geometry truth.

## Evidence levels

1. **Contract:** JSON fixtures validate, paths resolve, semantic IDs are unique.
2. **Host oracle:** Roborazzi renders every scene/profile and asserts the live
   Compose semantics nodes.
3. **Runtime oracle:** API 37 emulator capture provides actual OS rendering,
   accessibility XML, interaction and ambient/lifecycle behavior.
4. **LVGL simulator:** production source emits RGB565 screenshots, semantic
   snapshots and component/resource telemetry.
5. **Cross-renderer review:** normalized images, differences, overlays,
   boundaries and role/token reports are reviewed by story.
6. **Hardware:** T-Watch/CoreS3 provides frame time, memory, bus, touch, haptic,
   audio, sunlight and ergonomic truth.

Host oracle evidence alone cannot earn an `Exact` fidelity label. Exact also
requires a reviewed runtime oracle and per-state cross-renderer comparison.

## Determinism rules

- no real clock, network or random input in a capture;
- fixed locale and explicit text in reference fixtures;
- dynamic color disabled unless the fixture supplies a controlled system
  scheme;
- ambient and reduced-motion state supplied by the fixture, while the runtime
  app also responds to the real Wear ambient manager;
- screenshot filenames derive from scene, profile and phase;
- expected semantic labels are fixture data, while actual semantics are read
  from Compose;
- visual tolerances are per story and never conceal geometry errors.

## Initial gate

The checked-in first gate covers ten high-leverage scenes across small round,
large round and 240-square profiles. The next gate should add:

- pressed and mid-animation ButtonGroup captures;
- TransformingLazyColumn start/middle/end positions;
- EdgeButton entrance/exit;
- picker snap keyframes;
- alert-dialog navigation and swipe dismissal;
- large-font and long/multiline label variants;
- the ProtoLayout Material 3 Tile renderer;
- automated AppSpec-to-reference-scene lowering;
- cross-renderer bounds and token reports.
