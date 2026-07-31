# Timer parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers consume the same AppSpec snapshots and present
the same countdown hierarchy, geometry, state communication, interactions,
and minimum touch targets. Pixel identity is not required because Compose and
LVGL use different font rasterizers and edge-antialiasing paths.

The app intentionally has no launched-app title bar. The countdown owns the
full 240×240 framebuffer.

## Source material

The observations and provenance for the Apple Watch, Pixel Watch, and Wear OS
timer references are in
[`reference/inspiration/timer/README.md`](../inspiration/timer/README.md).
These images are research inputs only and are not shipped as product assets.

## Accepted states

| State | SceneSnapshot SHA-256 | Expected presentation |
|---|---|---|
| Resting | `e2d1fb677f4c73193b11ec3f53eef6f048b25c0d4938ba4b1830edced9bfb089` | Full progress dial, `1:00`, enabled minute stepper, `Start` |
| Running | `6ec4acfe60bb6646523f5342608b1242538644fda14d707437aa829834156292` | Full progress dial, `1:00`, hidden/disabled stepper, `Cancel` |
| Completed | `6225c7c2d5396995c019265af9d11b00dfa0cfc20419a195815dbac4b72b4c06` | Empty track, `0:00`, hidden/disabled stepper, `Dismiss` |

The decisive trace performs mount, start, and completion and retains one
screen with three distinct semantic states.

## Resting comparison

Command:

```bash
./doodad perfect-render timer \
  --profile watch_square_240 \
  --output target/parallax/timer-final
```

Measured result:

- 5 reference nodes, 5 product nodes, 5 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 8,160 of 57,600 pixels changed (14.1667%)
- MAE 13.2546; RMSE 48.6597

The visible delta is concentrated in numeral/text rasterization and
antialiasing at the progress and button edges. It does not change hierarchy,
relative emphasis, state communication, legibility, or touchability.

## Real Wear OS evidence

The runtime lane used:

- AVD `Wear_OS_Square`
- Wear OS 7 / API 37
- signed ARM64 Wear system image revision 1
- Android Emulator 37.1.11
- `wm size 240x240`
- `wm density 200` (1.25 density; 192×192dp logical viewport)
- Wear Compose 1.6.2

Captured emulator framebuffer hashes:

| State | PNG SHA-256 |
|---|---|
| Resting | `f18fa16c486b3812904c8d1040cae84dc7ac0dddfc1b5b1bf7f78796791c932a` |
| Running | `52ca4880702e9eb1e014d579b88c2208d8c0a83dad4327b07d490bafbd7ad05a` |
| Completed | `92a9c783ce18f755d4a2d05ba3d5d054cdbd78e15dd4c492cbad81cc30724629` |

The resting capture also records the accessibility tree, build fingerprint,
renderer build hash, exact snapshot hash, API level, framebuffer geometry,
and emulator revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural (`progress` + `stepper`), not keyed to the
  Timer app ID.
- The progress maximum follows the selected duration; remaining time is the
  current progress value.
- The hidden stepper remains in the snapshot during running/completed states
  so the renderer can select one stable pattern without remounting a different
  screen.
- The LVGL numeral fonts are deterministic checked-in subsets derived from
  the Android Studio Roboto/Roboto Flex files under their OFL license.
- The square adaptation preserves the circular countdown metaphor while using
  a wide bottom action instead of circular-display edge geometry.
