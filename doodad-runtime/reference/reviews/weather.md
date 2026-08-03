# Weather parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers consume the same AppSpec snapshots and present
the same location context, tonal hero surface, condition mark, temperature
hierarchy, forecast details, freshness state, and refresh interaction. Pixel
identity is not required because Compose and LVGL use different font
rasterizers and edge-antialiasing paths.

The location is app content rather than a generic launched-app title bar. The
forecast uses the full 240×240 framebuffer.

## Source material

The first-party Apple/Android guidance, Pixel Weather research images,
provenance, and design observations are in
[`reference/inspiration/weather/README.md`](../inspiration/weather/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
|---|---|---|---|
| Resting | `a4e29030836fabac2d380bfafa3301dcf6916c2c16b25d3d943c9d2363c64793` | `affeba4617201c261a78300f74c04d2cdd5098e00bfae875b5e10d6c4c0a1f48` | `72°`, current forecast, enabled refresh |
| Loading | `ebe77ed6db1696bc7b56a154998d38eccad71e9c222f678f55ea54ddaeefc5a7` | `8a928f22254389497467b604519c34a0ce9eafcca7d4c3b4258439522b47abc5` | Existing forecast retained, `Updating...`, visibly disabled refresh |
| Stale | `7e92bbed14af6cc2131fa55c223e1608ce07fbefed2550a99bf47f0f5e5b4179` | `c27e02b4839c631154e6d3715cc27bc836573eb4a8bc88e6370493fcc0d44577` | Existing forecast retained, `Cached - 12m`, refresh restored |
| Offline | `398bd8d89342b6d137861fa4586138f0134598e4646f6e55ec30ce6190bf49aa` | `d3b6df3b2e35aca4579972d809fdd0804515e5a416ef8faba268377cfedc158e` | Explicit offline condition, concise cached fallback, refresh restored |
| Recovered | `fab2f3c5677408c633195f351cd91a1a711222345a54613f69ffc9ac5703af82` | `02731a2a8a78db6182f9a44ae5dc88d1bab9736402a9eb2ce3cd3d1d9acc9a47` | New `71°` current data and `Updated now` |

The seven-checkpoint trace also includes the repeated loading transition from
offline data. Every checkpoint replays without invoking Wasm and attests to
the recorded snapshot, semantic tree, and product framebuffer.

## Resting comparison

Command:

```bash
./doodad perfect-render weather \
  --profile watch_square_240 \
  --output target/parallax/weather-final
```

Measured result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 8,605 of 57,600 pixels changed (14.9392%)
- MAE 14.7180; RMSE 49.8836

The visible delta is concentrated in text rasterization and the renderer-local
refresh glyph. It does not change hierarchy, relative emphasis, state
communication, legibility, or touchability.

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
| Resting | `a6f913a2f1841787072d207461e18a10b542a1b0117fdf42dac81b95898f288d` |
| Loading | `9dec5cfaeeecad7ee009d4616679579c890faa2f3662feb1ddb2c6e9279cef8f` |
| Stale | `d70355762470ac267df3b257cd6f7f90aeea316645d5b6039f8c0e377e63f6a0` |
| Offline | `fb8f906b5c2dd120ec57e299391c8f0b97eeedcfe0be8b8fc4875e8cc1d86f69` |
| Recovered | `b63ab6df1001aed14ae647904d7635dc9e8828315d3af9fe0d7bd9dc82be2ad2` |

The resting capture also records the accessibility tree, build fingerprint,
renderer build hash, exact snapshot hash, API level, framebuffer geometry,
and emulator revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural (one card, one button, and four text roles),
  not keyed to the Weather app ID.
- The current condition, temperature, forecast, freshness, and action remain
  separate semantic nodes even though the visual composition overlaps them on
  one hero surface.
- Loading retains useful cached content and disables the refresh action instead
  of replacing the screen with a spinner.
- All decisive-state strings fit without clipping or missing-glyph boxes.
- The LVGL temperature font is a deterministic checked-in Roboto Flex subset
  under the SIL Open Font License 1.1.
