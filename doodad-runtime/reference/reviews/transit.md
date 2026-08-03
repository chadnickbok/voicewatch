# Transit parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a Castro northbound
arrival and present the same full-departure, cached, recovered, and
service-delay states. All five accepted checkpoints have the same seven
semantic nodes and exact normalized bounds. Pixel identity is not required
because Compose and LVGL use different font rasterizers, RGB565 quantization,
and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Station and direction
context, one dominant arrival metric, compact line/platform detail, and two
decisive actions own the full 240×240 framebuffer. All visible labels fit in
both renderers.

## Source material

Citymapper's first-party Apple Watch departure/journey images and Apple's
official watch Maps image, with provenance and hashes, are in
[`reference/inspiration/transit/README.md`](../inspiration/transit/README.md).
They are research inputs only and are not shipped as product assets. Google's
current Wear guidance separately confirms public-transport routes, ETA,
overview, and upcoming-step access.

The oracle borrows the references' information hierarchy rather than their
branding or geometry: next arrival first, route/platform second, later
departures third, with stale and disruption status made explicit.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Nearby | `d119f671845d75a33d4c9fa3bd9a2254cd72e041ef5996a82e6218ced187bdee` | `3b0e1c680babe375bea79cbe41a09c27167aa153cf8332d80c37ad6eadd1d684` | Castro northbound, 3 min, N platform 2, later departures, Times, and Alert |
| Departures | `d59a7ced3ce6f7cb259fb3983ceb9a9a3c6bdce561ec0e86ac79561c60f05334` | `0ab40a868f7df8de642e3eb44a728b6c2b25926918d73b9b54e8005e240389d6` | Same live departure context with Offline and Alert actions |
| Cached | `03ea706d4edd86c277538f8aa4f80af39b93fa31f7fd5ed9dcba943dff43ec9e` | `ffeb7eba5526c1fdccbfdeee879b61e643e40eef946f8ab35c9062b07164a86d` | 18-minute-old cache, 2 min scheduled arrival, Retry, and Keep |
| Recovered | `ae3488898068817aa303da06d422c72001e0090a47eeced56df01e1a696bdd81` | `6153be4de48b01981309007e3dd54af9d82cce9fcea337483dce8ba229326e21` | Updated-now context, 4 min live arrival, preserved selection, Alert, and Times |
| Service alert | `8ac1b3de16794b947e73825c9e39a20aeeaf45149a1a5912209cc648bcac034b` | `766d563457ef2b624089d3469e6678d03afeef3f5cfb7001bdd2da6be14d15e4` | 6 min N-line delay, track-work context, Times, and Offline |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render transit
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,900 of 57,600 pixels changed (13.7153%)
- MAE 15.5162; RMSE 49.7651

Departures, cached, recovered, and alert states also have exact structure and
bounds with no quality findings. Their changed-pixel fractions are 13.9809%,
13.4826%, 14.2917%, and 14.9514%, with MAE 15.8819, 14.6475, 15.9898, and
16.7573 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and cached-state images.

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
[`transit.resting.watch_square_240.png`](../android-wear/captures/runtime/transit.resting.watch_square_240.png),
SHA-256
`d18f19114cf51099b2680791eb99413cfe461fe3c48a10273c3e85397a21e246`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Transit app ID.
- Wear uses Material 3 text, live card, progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The first comparison exposed two stale-state truncations. Shortening the
  context and action to `CACHE / 18 MIN` and `Retry` restored full legibility
  without changing the provider action.
- Arrival and service data remain deterministic fixtures. Actual agency feeds,
  location, alerts, and offline-schedule policy remain host/service concerns.
