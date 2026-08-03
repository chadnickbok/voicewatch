# Navigation parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into an eight-minute walking
route and present the same home, maneuver, cached-route, and GPS-recovered
states. All four accepted checkpoints have the same seven semantic nodes and
exact normalized bounds. Pixel identity is not required because Compose and
LVGL use different font rasterizers, RGB565 quantization, and
edge-antialiasing paths.

The app intentionally has no launched-app title bar. Route context, one
dominant distance or maneuver metric, a compact progress card, and two
decisive actions own the full 240×240 framebuffer. All visible labels fit in
both renderers.

## Source material

Google's official Wear OS Google Maps and offline-maps images, with provenance
and hashes, are in
[`reference/inspiration/navigation/README.md`](../inspiration/navigation/README.md).
They are research inputs only and are not shipped as product assets.

The oracle borrows the references' hierarchy rather than their round geometry:
a dominant next instruction, compact arrival and route information,
high-contrast controls, and explicit offline/GPS state communication. The
square Doodad profile preserves that behavioral hierarchy without pretending
its display is circular.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Route ready | `0c092e2f64b5f6ec2fe2f44d93664118c1194f59ce72d117cc5750a37ba4bff0` | `8288c900898f884d41fd1aad175af2ea8d56cfb4fdcea183b08f1b126557b515` | 8 min, 1.4 mi, 10:18 arrival, offline route, Start, and Route |
| Maneuver | `b623ec3aa7b85e79016f51048d9d48affff7991336b62ccfcc670b9f3a78d22c` | `59a4efb3c2cf073ca41560942076c77f7c6dfedafab7098b93fa213dd655701a` | 200 ft, turn-right instruction, haptic-soon detail, GPS off, and Route |
| Cached GPS | `94b08417e3cdaa92c1c2dd9a69c30c034e647e65d87c51f06d811b96c005e685` | `596a52f80b103d82f461877579a160dc1680788da32740c98af87ad981ac69ad` | 0.3 mi, cached route and compass context, Recover, and Route |
| GPS recovered | `b8153cfc1d556528247ef1e4d6312b7ed49fedd99841644768652dfe6f98401c` | `ba6a173b1061768f20a62062833fd9e8e03888bc99095517aefaaa3d982286bb` | 120 ft, progress-preserved detail, Next, and Home |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer. A fifth authored route
overview document is also valid and renderable, but the accepted decisive flow
does not currently visit it.

## Comparisons

Resting command:

```bash
./doodad perfect-render navigation
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 8,073 of 57,600 pixels changed (14.0156%)
- MAE 15.4970; RMSE 49.2916

Maneuver, cached-GPS, and recovered-GPS states also have exact structure and
bounds with no quality findings. Their changed-pixel fractions are 13.7656%,
15.1233%, and 14.6493%, with MAE 14.8749, 16.9360, and 16.5817 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and maneuver-detail images.

## Real Wear OS evidence

The runtime lane used:

- AVD `Wear_OS_Square`
- Wear OS 7 / API 37
- signed ARM64 Wear system image revision 1
- Android Emulator 37.1.11
- `wm size 240x240`
- `wm density 200` (1.25 density; 192×192dp logical viewport)
- Wear Compose 1.6.2

The accepted API 37 framebuffer is
[`navigation.resting.watch_square_240.png`](../android-wear/captures/runtime/navigation.resting.watch_square_240.png),
SHA-256
`ca05a2f28d5015c0ff8748119b2cf56ecc4ec27f1c746c54f2965e8ebe0c0316`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Navigation app ID.
- Wear uses Material 3 text, live card, progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The comparison exposed a missing period in the 32px LVGL live-action font.
  The shared generated font now includes the glyph, and the framework test
  prevents its regression.
- Route data and GPS state remain deterministic app fixtures. Actual location,
  compass, offline-map storage, and background-navigation policy remain
  host/system concerns.
