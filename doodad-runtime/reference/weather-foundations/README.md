# Weather foundations v1

This directory is the canonical source and generated review surface for the
Weather foundations and reusable-component milestones: icons, typography,
semantic color, square shape roles, and the first dual-renderer composition.

## Decisions

### Typography: Roboto

Weather uses **Roboto Medium (500) for every role**. Wear Compose already
renders these roles at weight 500, and LVGL rasterizes the pinned hinted
Roboto Medium source in `vendor/roboto/`. The existing Weather hero font was
already Roboto-based. Montserrat remains the framework fallback, but is not
mixed into Weather screens.

Eleven bounded 4bpp LVGL subsets are checked in. The 1.3x accessibility
profile scales micro, label, row, metric, and headline roles; the 68px hero is
intentionally capped because an 88px temperature would displace the current
condition and primary action on a 240px display.

| Role | Normal / large physical size | Intended use | Generated C total |
| --- | ---: | --- | ---: |
| micro | 10 / 13px | chart times, units | 59,205 B |
| label | 14 / 18px | supporting labels | 78,990 B |
| row | 18 / 23px | forecast rows, actions | 104,147 B |
| metric | 28 / 36px | metric values | 180,973 B |
| headline | 32 / 42px | rain headline | 225,876 B |
| hero | 68px capped | temperature digits | 55,835 B |

The generated C sources total **705,026 bytes**; this is a source-size measure,
not the linked ESP32-S3 flash cost. The firmware link measurement is recorded
at the hardware gate, and unreferenced static-library font objects are not
pulled into an app binary.

The repertoires and required golden strings live in
`weather-foundations-v1.json`. `generate_fonts.py --check` verifies the LVGL
symbol, provenance, hash, and every required non-space codepoint without
requiring Android Studio in CI.

### Icons

- 16 condition icons cover codes 0 through 15 without gaps.
- 17 utility icons cover location, navigation, high/low, metrics, freshness,
  details, warning/offline, refresh, and retry.
- Weather conditions use the pinned **Meteocons Flat** package. Dedicated
  sunrise and sunset illustrations also remain Meteocons.
- Controls and status affordances use pinned **Material Symbols Rounded 400**;
  this includes location, metrics, warning/offline, refresh, and retry. These
  single-color sources carry a semantic Weather tint role in the catalog.
- Only the selected upstream SVGs and their licenses are vendored. The
  generator emits review SVGs, 64px transparent rasters for the LVGL asset
  pipeline, 128px Android drawables, and renderer catalog metadata.
- Required review sizes are 16, 18, 24, 32, 48, 64, and 72dp.

The Compose oracle's `WeatherGlyph` now reads the generated Android resources.
The LVGL renderer now uses generated, size-specific 18/24/32/64px assets.
Every shipping size is rasterized directly from the pinned vector source;
the 64px review PNG is no longer an intermediate for smaller device assets.
Multicolor Meteocons are packed as RGB565A8. Recolorable utilities use A8
masks: raw A4 descriptors clipped under LVGL 9.5's software image path during
Phase 2 integration, while the bounded A8 set rendered correctly without a
runtime decode or scaling allocation. The first CoreS3 flash and memory
measurement is recorded in `generated/phase6-hardware/`; T-Watch and
interaction measurements remain.

### Color

Thirty semantic roles define the dark Weather scheme, icon colors, freshness
states, and ambient fallback. Generated C++ and Kotlin outputs contain both
RGB888 source values and exact RGB565 values. Every declared text pair passes
its required contrast after RGB565 quantization; see `generated/contrast-report.md`.

Fresh, stale, offline, and error remain separate semantic states. Product UI
must still pair them with text or iconography rather than rely on color alone.

### Square shapes

Ten roles cover compact/status pills, forecast rows, chart/hour surfaces, hero
surfaces, three expressive metric variants, and the inset bottom action. The
tokens store four corners independently, so asymmetric shapes do not become
renderer-local magic numbers. Metric C remains a cut-corner experiment and
must pass LVGL antialiasing and mask-cost review before app signoff.

## Canonical and generated files

- `weather-foundations-v1.json` — semantic token and pinned icon mapping source
- `vendor/meteocons-flat/` and `vendor/material-symbols-rounded/` — curated
  upstream SVGs, licenses, and exact package provenance
- `generated/foundation-gallery.html` and `.png` — review gallery
- `generated/icons/` — 33 deterministic SVGs
- `generated/raster-64/` — transparent 64px asset-pipeline inputs
- `generated/contrast-report.md` — RGB888/RGB565 contrast results
- `generated/phase2/weather-side-by-side.png` — API 37 Wear emulator and LVGL
  desktop simulator capture from the same runtime snapshot
- `weather-app-master.json` — five-screen visual master used to generate the
  bounded route AppSpecs
- `generated/phase4-complete/report/` — Current, Hourly, Daily, Details, and
  imminent-rain dual-renderer comparisons
- `generated/phase5-live-data/report/` — the same five oracle screens after
  provider-v2 Wasm formatting, atomic chart/icon patches, and retry recovery
  were wired through the production scene trace
- `generated/phase5-live-data/concept-compose-lvgl-contact-sheet.png` — the
  five approved concepts beside the real Wear Compose oracle and LVGL output
  after the larger hero-art, optical font-leading, direct vector raster, and
  compact-visual-action passes; the action semantics retain their full 48dp
  bounds
- `generated/phase5-states/report/` — baseline, extreme heat, imminent rain,
  stale, and error captures from deterministic provider-v2 payloads
- `generated/phase5-large-font/report/` — all five screens at the bounded 1.3x
  font profile, including adaptive Daily, Details, and rain compositions
- `generated/phase6-hardware/` — calibrated CoreS3 photograph, desktop/physical
  comparison, stable firmware telemetry, and the accepted memory-placement
  checkpoint
- `generated/phase2/wear-runtime/` — emulator PNG, semantic dump, and runtime
  provenance manifest
- `generated/font-manifest.json` — font hashes, codepoints, and source sizes
- `components/m3e_lvgl/include/m3e/generated/weather_tokens.hpp`
- `components/m3e_lvgl/include/m3e/generated/weather_icons.hpp`
- `reference/android-wear/.../generated/WeatherFoundations.kt`
- `reference/android-wear/.../generated/WeatherIcons.kt`
- `reference/android-wear/app/src/main/res/drawable-nodpi/weather_icon_*.png`

Regenerate and verify:

```sh
python3 tools/weather_foundations/generate.py --generate
python3 tools/weather_foundations/generate.py --check
python3 tools/weather_foundations/generate_fonts.py --check
python3 tools/weather_foundations/generate_appspecs.py --check
```

Icon regeneration requires `rsvg-convert` from librsvg. The vendored SVGs make
normal Android/native builds independent of npm and network access.

Font regeneration is intentionally explicit because it invokes the pinned
`lv_font_conv` tool; its Roboto Medium source is vendored:

```sh
python3 tools/weather_foundations/generate_fonts.py --generate
```

## Phase 2 component status

Phase 2 adds semantic `icon`, `surface`, `chart`, and `pager` AppSpec nodes,
real generic button icons, deterministic stretch rows, and atomic bounded chart
series patches. The same 27-node Weather AppSpec now renders through Wear
Compose on the API 37 square emulator and LVGL on the desktop simulator. Pager
selection is swipe-driven in both renderers and can also be changed with the
bounded integer command property.

The Phase 5 split-screen product preserves that behavior without mounting the
oversized five-page document. Every bounded screen exposes the same root
`pageChanged` action; left/right gestures in Compose and LVGL emit signed page
deltas to the Weather Wasm guest, which owns the forward/back route policy and
the imminent-rain branch.

The current optical pass also implements the expressive cut-corner UV tile in
LVGL with a custom bounded polygon fill, corrects the Details icon source-scale
math, and gives swipe-mounted routes a 220ms directional entry with a 160ms
fade. The checked Details comparison is now 13.61% changed pixels with 10.34
MAE. Rain uses the same fractional 192dp geometry and physical-pixel
centered-bar calculation as Compose; its compact visual action floats inside
an unchanged 48dp hit target. That pass reduces Rain from 16.53% / 14.55 MAE /
35 structured mismatches to 10.73% / 8.29 / 17.

That Phase 2 component-integration composition has now been superseded by the
five-page Phase 5 product. Current, Hourly, Daily, Details, and imminent rain
execute through the real Wasm guest, and shared semantic label/value patches
keep accessibility snapshots atomic with visible provider data. The bounded
1.3x font profile now has a complete five-screen dual-renderer report. CoreS3
first-paint and stationary memory are measured; route motion, touch, T-Watch
S3, and power remain hardware conformance work.

The Phase 6 desktop host also has a real Open-Meteo adapter in
`tools/weather_provider/`. It normalizes live city forecasts into the same
provider-v2 payload, persists last-good data, and can render four live routes
through the production Wasm/LVGL path. Live captures stay under `target/` so
time-varying API output cannot silently become a design golden.

The matching ESP-IDF adapter is opt-in under the `Doodad Weather provider`
menuconfig section. It performs Wi-Fi/HTTPS work on an asynchronous worker,
uses configured coordinates rather than granting network or location access
to Wasm, validates through the same bounded provider-v2 encoder, and stores a
versioned last-good record in NVS. Both deterministic-default and
network-enabled firmware configurations link successfully; live hardware
network/cache trials remain part of the Phase 6 acceptance work.
