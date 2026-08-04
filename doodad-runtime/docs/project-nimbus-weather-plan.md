# Project Nimbus: square Weather app pre-build plan

Document role: implemented project record and focused hardware-closure tracker
Status: implemented through the app, renderer, and provider slices; hardware closure pending
Last reconciled: 2026-08-04
Target: Doodad 240×240 physical / 192×192dp square display
Reference renderer: Wear Compose Material 3 Expressive
Product renderer: LVGL RGB565
Guest runtime: Rust `no_std` WebAssembly

This document no longer sets overall project order. See the
[Doodad roadmap](roadmap.md). The unchecked Phase 6 hardware, endurance, and
final-review items remain valid Weather follow-up work.

## 1. Outcome

Build a title-free Weather app whose real Wasm guest can render the five square
concepts in `reference/inspiration/weather/generated-mockups/` through the same
semantic AppSpec in both the Wear reference renderer and the LVGL production
renderer.

The five visual concepts are:

1. current conditions;
2. hourly forecast;
3. daily forecast;
4. weather details;
5. imminent rain.

The fifth is a data-driven state, not a permanent navigation destination. When
rain is imminent it replaces or augments the normal hourly page.

This plan deliberately prepares reusable components, icon geometry, data
contracts, fixtures, and conformance cases before assembling the app. The
generated PNG concepts are visual targets; they are not shipping assets and
they are not yet proof of touch geometry, text scaling, or hardware cost.

## 2. Product decisions to lock first

- The app owns the complete display. There is no generic Weather title bar.
- The location is useful content and remains visible where it disambiguates
  the forecast.
- The primary screen answers “what is it like now?” in one glance.
- Refresh is automatic on launch/foreground and power-aware thereafter.
- Freshness is normally quiet text. The entire freshness chip has a compliant
  hit region and can request a manual refresh.
- Stale or failed data promotes the low-emphasis freshness affordance into an
  explicit `Retry` action; a permanent floating refresh disc is not used.
- Last-known useful weather remains on screen during refresh and offline
  states.
- Condition glyphs are deterministic renderer assets generated from one
  canonical vector description. They are not AI-generated raster art.
- Weather charts are theme-aware semantic components. The raw game canvas is
  not the default chart implementation.
- AppSpec remains semantic: a guest does not name colors, coordinates, fonts,
  radii, LVGL widgets, or Compose classes.
- The first implementation uses deterministic fixtures. A real network and
  location provider is a separate host milestone.

## 3. What already exists

Legend: **Ready** can be reused as-is or with small styling work; **Partial**
has the right seam but is not sufficient; **Missing** needs a new contract or
implementation.

| Requirement | Status | Existing source | What it means for Nimbus |
|---|---|---|---|
| Full-screen, title-free launch | **Ready** | `docs/appspec-v1.md` launch-surface policy | Weather can use all 240×240 pixels; location is allowed as context. |
| Semantic screen/text/row/column/scroll | **Ready** | `contracts/appspec-v1.*`, `tools/doodad_cli/appspec.py` | Basic hierarchy is available and bounded. |
| Buttons, cards, progress, semantics | **Ready** | `components/m3e_lvgl/components`, AppSpec v1 | Useful primitives exist, but some need composition work below. |
| Alternate AppSpec screens from Wasm | **Ready** | `mount_appspec`, multi-screen Calendar/Media packages | The guest can navigate by mounting another bounded screen. |
| Horizontal pager and page indicator | **Partial** | `ComponentFactory::horizontal_pager*`, `component-matrix.yaml` | LVGL prototypes exist, but pager is not exposed as an AppSpec component and has no dual-renderer contract. |
| Current Weather hero | **Partial** | `apps/weather/appspec.json`, `is_weather_hero_document`, `SquareWeatherHeroSurface` | One bespoke six-child composition exists. It does not represent the five new screens. |
| Current temperature font | **Partial** | `m3e_weather_font_55` | Roboto Flex digits, minus, and degree exist at one display size; labels and values still fall back to Montserrat. |
| General typography | **Partial** | Material typography tokens; LVGL 10/14/16/18 Montserrat mapping | Role mapping exists, but font family, role sizes, line metrics, and glyph coverage do not match the concepts. |
| Theme/color roles | **Partial** | `ResolvedTheme`, `ReferenceTheme`, Material 1.6.2 tokens | Role infrastructure exists, but current Weather hard-codes violet colors and the blue Weather scheme is not a checked token set. |
| Expressive shapes | **Partial** | shape-morph prototype and renderer-local rounded rectangles | Radius helpers exist, but Weather tile shape roles and asymmetric families are not defined. |
| Generic icon library | **Partial** | `IconName` has 12 LVGL-symbol icons | There is no AppSpec `icon` node, no Weather glyph set, and button `icon` is not generally rendered. Current refresh is bespoke drawing. |
| Package images | **Partial** | AppSpec `image`, DIMG packaging, Compose DIMG decoder | Opaque RGB565 image packages work, but LVGL resolves only three statically registered assets. No transparent/recolorable icon pipeline exists. |
| Multimedia reference | **Ready for other apps** | Media, Remote Control, Wallet DIMG assets | Proves hashed images work; Weather does not need photos for v1. |
| Canvas | **Partial / current worktree** | Canvas Display List v1 used by Snake | Bounded drawing exists, but raw palettes are app-owned and the 128-character display list is unsuitable for semantic Weather charts without extension. |
| Linear/circular progress | **Ready** | `progress` AppSpec and component factory | Useful for simple percentages, not time-series graphs. |
| Precipitation graph | **Missing** | none | Needs a bounded semantic chart with accessible summary and dual renderers. |
| Composable card/surface | **Missing** | `card` accepts title/body only | Forecast rows and metric tiles need a tonal container that can own icon/text children. |
| Equal-width metric grid | **Partial** | row/column exist | A 2×2 grid can be composed once child cards and deterministic equal sizing work. |
| Freshness model | **Ready** | provider envelope current/stale/offline/error; Weather status formatter | Preserve and expand it across every Weather page. |
| Loading/stale/offline/recovery flows | **Ready at old scope** | Weather decisive trace and `reference/reviews/weather.md` | The lifecycle semantics are good and should be retained for the richer UI. |
| Weather provider data | **Partial** | `weather-state-v1` and SDK decoder | Carries only temperature, condition string, one detail string, location, revision, and age. |
| Real Weather backend | **Ready, opt-in on device** | `tools/weather_provider/`, `tools/fetch_weather.py`, `firmware/main/src/weather_provider.cpp` | Desktop and ESP-IDF hosts normalize Open-Meteo into the same bounded provider-v2 snapshot. The device adapter owns HTTPS, configured coordinates/timezone/units, async delivery, and versioned NVS last-good cache. Power-aware scheduling and live hardware network trials remain. |
| Dual-renderer screenshot pipeline | **Ready** | Project Parallax scripts, Wear API 37 square AVD, LVGL simulator | Extend from one Weather screen to all pages/states. |
| Hardware performance evidence | **Partial** | `reference/weather-foundations/generated/phase6-hardware/` | CoreS3 Current has physical, object, first-paint, flash, and heap evidence. Route motion, touch, power, and T-Watch S3 remain. |

### Existing Weather implementation that will be replaced

The present `apps/weather/appspec.json` contains exactly:

- one location label;
- one title/body forecast card;
- one text node standing in for a condition symbol;
- one large temperature;
- one freshness label;
- one circular refresh button.

Both renderers recognize that exact structure and switch to a hard-coded
Weather layout. The reference implementation is
`SquareWeatherHeroSurface`; the product implementation begins at
`is_weather_hero_document`. This was appropriate for proving parity, but it is
not an extensible component model for Nimbus.

## 4. Screen anatomy

### 4.1 Current conditions

Required semantic content:

- location context;
- current condition glyph;
- current temperature;
- condition label;
- daily high and low;
- feels-like temperature;
- freshness/retry state;
- navigation to hourly forecast.

Required components:

- `WeatherHero` composition;
- location icon + label row;
- large `ConditionGlyph`;
- large numeral text role;
- two compact metric pills for high/low;
- one supporting metric row for feels-like;
- tappable `FreshnessChip`;
- `BottomAction` pill.

### 4.2 Hourly forecast

Required semantic content:

- current summary;
- next-hour precipitation probability;
- bounded time-series precipitation values;
- four glanceable hourly forecast entries;
- freshness state;
- navigation to daily forecast.

Required components:

- compact current-condition header;
- `PrecipitationChart`;
- four equal-width `HourlyForecastTile` surfaces;
- optional selected/current tile state;
- `FreshnessChip`;
- `BottomAction`.

The 240×240 implementation should simplify the generated concept by removing
nonessential y-axis labels. `0%`, `50%`, and `100%` are attractive at mockup
scale but become microtext on hardware. A baseline, bars/line, and one spoken
summary are sufficient.

### 4.3 Daily forecast

Required semantic content for four days:

- weekday/current-day label;
- condition glyph and accessible condition name;
- low temperature;
- high temperature;
- optional precipitation probability;
- selected/current-day emphasis;
- freshness state.

Required components:

- location context row;
- four `DailyForecastRow` tonal surfaces;
- selected expressive state for today;
- optional scroll container at font scale 1.3;
- `FreshnessChip`.

### 4.4 Weather details

Initial four metrics:

- humidity;
- wind speed and direction;
- UV index and category;
- sunrise time.

Future-compatible metrics:

- visibility;
- pressure/trend;
- dew point;
- sunset;
- air quality;
- gust speed.

Required components:

- compact current-condition header;
- two rows of two equal-width `MetricTile` surfaces;
- deterministic shape variants by tile position;
- metric icons;
- `FreshnessChip`.

### 4.5 Imminent rain state

Required semantic content:

- rain condition glyph;
- minutes until rain begins;
- expected duration;
- probability;
- 60-minute precipitation series;
- freshness state;
- navigation to details/hourly view.

Required components:

- large condition glyph;
- alert-style headline that remains calm rather than error-colored;
- probability summary row;
- `PrecipitationChart` in bars mode;
- `BottomAction`;
- `FreshnessChip`.

If there is no meaningful minutely data, the app must not show an empty chart;
it falls back to normal hourly content.

## 5. Component work to finish before app assembly

### P0.1 AppSpec icon node

Add a generic semantic `icon` component instead of using text, a button-only
icon string, or renderer-local decorative drawing.

Proposed semantic properties:

- `name`: bounded registry identifier;
- `size`: `small`, `medium`, `large`, or `hero`;
- `tone`: semantic Material tone;
- optional `state`: normal, selected, disabled;
- required semantic label when the icon carries meaning;
- empty semantics only when it is explicitly decorative.

The same icon registry must feed Compose and LVGL. Unknown icons fail
validation before mount.

### P0.2 Composable tonal surface

Extend `card` or introduce `surface` as a layout container that may own bounded
semantic children. Preserve the existing title/body card form for backward
compatibility.

Needed behavior:

- Material tone and content color pairing;
- selected/pressed/disabled states;
- renderer-owned shape role;
- equal expansion in a row;
- optional click action and 48dp hit region;
- clipping and text overflow rules;
- semantic grouping without hiding child semantics.

This one capability unlocks daily rows, hourly tiles, metric tiles, freshness
chips, and current high/low pills without five app-specific wire components.

### P0.3 Bounded semantic chart

Add a small, theme-aware chart component rather than rendering Weather through
the game canvas.

Minimum chart contract:

- styles: `line`, `bars`;
- 1–13 unsigned samples;
- explicit maximum, normally 100;
- semantic tone;
- optional current/peak marker;
- required accessibility summary such as “Rain chance rises to 70 percent in
  30 minutes”;
- atomic series replacement from a CommandBatch;
- no arbitrary colors, labels, fonts, or coordinates from Wasm.

Hard bounds:

- maximum 13 samples for five-minute buckets over one hour;
- maximum one chart per screen;
- no axes unless the renderer can keep text at the minimum legible size;
- settled render must not allocate a full 240×240 ARGB buffer;
- invalid samples reject the entire patch.

The renderer can implement this with LVGL draw events/line objects and Compose
Canvas while exposing the same semantic node and evidence bounds.

### P0.4 Generic button icons

Make the existing AppSpec `button.icon` property real in both renderers.

Nimbus immediately needs:

- `chevron_right` for bottom actions;
- `refresh` only for retry/error states;
- optional `list` for Details.

The label remains present for semantics even if a future compact variant hides
it visually.

### P0.5 Equal-width row behavior

Define deterministic equal expansion for two- and four-child rows. Test:

- two high/low pills;
- four hourly tiles;
- two metric tiles per row;
- long text and font scale 1.3;
- 48dp hit bounds where children are interactive.

### P1.1 AppSpec horizontal pager

Expose the existing pager prototype as a semantic layout component after the
five individual screens work.

Proposed behavior:

- 2–5 child pages;
- selected index as bounded integer state;
- `pageChanged` event;
- direct programmatic selection from a bottom action;
- reduced-motion transition;
- optional page indicator;
- lazy mounting or strict offscreen object budget.

Initial app assembly may use separate AppSpecs and explicit actions. Pager
support should replace that temporary navigation before final hardware signoff
so horizontal navigation is natural and no title-bar back button is required.

### P1.2 Weather composites

Implement the following as renderer-side compositions built from the generic
primitives above, not as new public Wasm widget types unless the structural
composition proves impossible:

- `WeatherHero`;
- `HourlyForecastTile`;
- `DailyForecastRow`;
- `MetricTile`;
- `FreshnessChip`;
- `BottomAction`;
- `ConditionSummaryHeader`.

Each composite needs a catalog story, interaction state story, semantic tree,
Wear screenshot, LVGL screenshot, RGB565 comparison, and object-count record.

## 6. Icons and imagery to generate

### 6.1 Canonical condition glyph set

Use a pinned, curated Meteocons Flat vector for each condition:

| Code | Glyph | Initial need |
|---:|---|---|
| 0 | clear day | yes |
| 1 | clear night | yes |
| 2 | partly cloudy day | yes |
| 3 | partly cloudy night | yes |
| 4 | cloudy | yes |
| 5 | overcast | yes |
| 6 | fog/haze | yes |
| 7 | drizzle | yes |
| 8 | rain | yes |
| 9 | heavy rain | yes |
| 10 | thunderstorm | yes |
| 11 | snow | yes |
| 12 | sleet/freezing rain | yes |
| 13 | wind | yes |
| 14 | hot/extreme sun | later, but reserve code |
| 15 | unknown/unavailable | yes |

Required rendered sizes:

- 16–18dp supporting icon;
- 24dp forecast-row icon;
- 32dp compact summary icon;
- 48dp tile icon;
- 64–72dp hero icon.

The condition glyphs retain Meteocons' professional multitone palette. Utility
symbols remain role-tinted by the Weather semantic palette:

- sun/lightning: tertiary/accent role;
- cloud/snow: on-surface and surface-variant roles;
- rain: primary/secondary role;
- night: primary-dim role;
- unavailable: neutral/error role according to state.

### 6.2 Utility icon set

Use Material Symbols Rounded 400 for the utility set, except the dedicated
Meteocons sunrise and sunset illustrations:

- location pin;
- chevron right;
- arrow up/high;
- arrow down/low;
- thermometer/feels like;
- droplet/humidity;
- wind;
- UV sun;
- sunrise;
- sunset;
- clock/freshness;
- precipitation bars;
- list/details;
- warning/offline;
- refresh/retry.

### 6.3 Asset pipeline

Preferred pipeline:

1. Pin and vendor only the selected Meteocons and Material Symbols SVGs plus
   their licenses and archive hashes.
2. Validate source, render mode, semantic tint role, condition-code coverage,
   and absence of embedded text.
3. Generate Android drawable PNGs and Compose catalog metadata.
4. Generate 64px transparent raster sources and LVGL catalog metadata; pack
   final multicolor assets as RGB565A8 and utilities as recolorable bounded
   alpha masks. Phase 2 uses size-specific A8 utilities because LVGL 9.5's
   software path clipped raw A4 image descriptors in simulator validation.
5. Render a size-and-theme gallery from both implementations.
6. Quantize the Compose gallery to RGB565 and compare it to LVGL.
7. Check small-size silhouette, no clipping, night/ambient variants, flash,
   decode cost, and peak draw memory.

Do not ship the 1254×1254 generated mockups or crop their icons. They are
style references, not clean vector masters.

### 6.4 When DIMG is appropriate

DIMG is not required for Nimbus v1. Use it later only for radar maps,
photographic backgrounds, or provider imagery.

Before that future use, close two existing gaps:

- product asset resolution must load package-declared hashes rather than only
  the three statically embedded demo images;
- the opaque RGB565 format needs either an alpha/mask extension or a clearly
  specified precomposited-background policy.

## 7. Typography to generate

The concepts read as Material largely because of Roboto-like proportions.
Current LVGL Weather uses Roboto Flex only for one numeral role and Montserrat
for everything else.

Generate deterministic LVGL Roboto/Roboto Flex subsets for:

| Role | Nominal use | Required repertoire |
|---|---|---|
| Weather hero | 56-ish display | digits, minus, degree |
| Weather headline | 30–34 | letters, digits, degree, `%` |
| Metric value | 26–32 | letters, digits, degree, `%`, `:`, period |
| Title/row high | 17–20 | upper/lowercase, digits, punctuation |
| Body/labels | 12–16 | normal UI repertoire |
| Micro label | 10 minimum | weekdays, times, units only |

The exact size is chosen from real 192dp/240px measurement, not copied from the
1254px mockup.

Golden strings must include:

- `-9°`, `0°`, `62°`, `105°`;
- `100%`, `8 mph`, `12 km/h`;
- `6:12`, `12:00`;
- `Partly cloudy`, `Thunderstorms`;
- `San Francisco`, plus a deliberately long location;
- `Updated now`, `Cached · 59m`, `Weather unavailable`.

Before merging, record flash cost per subset and validate every visible corpus
codepoint. Prefer a small shared Roboto role family over more one-off app fonts
if the measured flash budget permits it.

## 8. Weather theme and geometry tokens

Create a checked Weather scheme in semantic roles rather than hard-coded
renderer RGB values.

Needed roles:

- background;
- on-background;
- primary and on-primary;
- primary container and on-primary container;
- secondary container and on-secondary container;
- tertiary sun/accent and on-tertiary;
- surface low/default/high;
- outline variant;
- error/error container;
- disabled and freshness status states.

Required checks:

- RGB888 source and generated RGB565 values;
- contrast after RGB565 quantization;
- selected/unselected forecast-row contrast;
- yellow sun against every allowed surface;
- blue rain against every allowed surface;
- stale/offline/error distinction without relying on color alone;
- monochrome ambient fallback.

Square Weather shape family:

- compact pill: fully rounded;
- forecast row: 18–24dp radius;
- hero surface: 28–32dp radius;
- metric tile A: rounded rectangle;
- metric tile B: asymmetric/superellipse-like variant;
- metric tile C: clipped/angled expressive variant only if LVGL cost and
  antialiasing remain acceptable;
- bottom action: pill with renderer-owned square-edge inset.

Use the existing 4/8/12/16/24 spacing rhythm. Every geometry value is resolved
by the display profile; Wasm does not author pixels.

## 9. Weather provider v2

### 9.1 Why v1 is insufficient

`weather-state-v1` contains six fields and cannot represent feels-like,
structured high/low values, metrics, hourly/daily arrays, condition codes, or
minutely precipitation. Encoding these into the existing `detail` string would
make layout, localization, semantics, and charts impossible.

### 9.2 Proposed bounded payload

Add a versioned `weather.snapshot.v2` provider event. Keep the envelope's
existing freshness, provider revision, and scenario timestamp.

Conceptual CDDL:

~~~text
weather-state-v2 = {
  0 => 2,                         ; payload schema version
  1 => tstr .size (1..48),        ; display location
  2 => 0..6,                      ; local weekday
  3 => 0..1439,                   ; local minute of day
  4 => weather-current-v2,
  5 => [1*7 weather-hour-v2],
  6 => [1*4 weather-day-v2],
  7 => [13*13 0..100],            ; five-minute precipitation buckets
  8 => int,                       ; minutes until rain, -1 when absent
  9 => uint,                      ; expected rain duration minutes
  10 => 0..1,                     ; metric / imperial
  11 => uint,                     ; authoritative data revision
  12 => uint,                     ; cache age minutes
}

weather-current-v2 = [
  int,                            ; temperature tenths
  int / null,                     ; feels-like tenths
  0..15,                          ; condition code
  int / null,                     ; high tenths
  int / null,                     ; low tenths
  0..100 / null,                  ; precipitation probability
  0..100 / null,                  ; humidity
  uint / null,                    ; wind speed tenths
  0..359 / null,                  ; wind direction degrees
  uint / null,                    ; UV index tenths
  0..1439 / null,                 ; sunrise local minute
  0..1439 / null                  ; sunset local minute
]

weather-hour-v2 = [
  0..1439,                        ; local minute of day
  int,                            ; temperature tenths
  0..100 / null,                  ; precipitation probability
  0..15                           ; condition code
]

weather-day-v2 = [
  0..6,                           ; local weekday
  int,                            ; low tenths
  int,                            ; high tenths
  0..100 / null,                  ; precipitation probability
  0..15                           ; condition code
]
~~~

This should remain comfortably inside the 512-byte provider payload limit,
but a canonical worst-case encoded-size test is mandatory. Use integer tenths
and enums rather than repeated strings.

### 9.3 SDK and host changes

- Add fixed-capacity `WeatherProviderPayloadV2`, current/hour/day structs, and
  a strict canonical decoder to the Rust SDK.
- Add matching C/C++ encoder used by desktop and firmware fixtures.
- Reject array overflows, invalid enums, invalid percentages, noncanonical
  maps, and trailing bytes.
- Keep the existing v1 decoder while migration tests run.
- Upgrade native-host and firmware deterministic fixtures together.
- Make refresh operation idempotent while one request is pending.
- Preserve last-good current data for stale/offline/error delivery.

### 9.4 Real provider milestone

The real host-owned provider later needs:

- location permission and a manually configured fallback city;
- geocoding/display-name policy;
- network Weather API adapter and credentials outside guest packages;
- normalized condition-code mapping;
- metric/imperial preference;
- timezone/local-time normalization;
- persisted last-good snapshot and age;
- foreground and power-aware refresh policy;
- retry/backoff and rate limiting;
- explicit source timestamp;
- deterministic record/replay fixtures with secrets removed.

Wasm receives only normalized bounded data. It does not receive network or GPS
access.

## 10. Wasm guest state and behavior

Use a fixed-capacity, allocation-free Weather state:

- one current-conditions struct;
- seven hourly records;
- four daily records;
- thirteen precipitation samples;
- fixed buffers for formatted strings;
- current page/index;
- last data revision and freshness;
- pending-refresh flag.

Behavior:

1. Mount the most useful cached/current screen immediately.
2. Request refresh on launch when host policy allows it.
3. Keep old values visible and mark freshness `Updating…`.
4. On v2 snapshot, validate and replace the state atomically.
5. Patch only the visible page or remount the destination page from retained
   state; do not emit an oversized all-pages CommandBatch.
6. On page navigation, populate that page from retained state before display.
7. On stale/offline/error, preserve data and change freshness/action semantics.
8. On invalid provider data, reject it and retain the previous revision.

The current 512-byte Weather `UiCommandBuffer` is probably too small for a
rich page. Measure encoded commands and raise only this guest's static buffer
within the ABI's 4096-byte result bound.

## 11. Interaction model

### Primary navigation

Target flow:

~~~text
Current ──swipe/tap──> Hourly ──swipe/tap──> Daily ──swipe/tap──> Details
   ^                                                              │
   └──────────────────── reverse swipe / page selection ──────────┘
~~~

Imminent rain is the Hourly page's high-value variant and may also replace the
Current page's lower detail region.

### Refresh

- Launch/foreground refresh is automatic.
- Current data: quiet `Updated now` chip; tapping it requests refresh.
- Loading: `Updating…`, disabled against duplicate requests; cached content
  remains.
- Stale: `Cached · 12m`, tappable refresh semantics.
- Offline: `18m cached · Offline`, retry remains available.
- Error without cache: explicit full-width `Retry` action and concise empty
  state.

### Motion and haptics

- Use existing expressive spatial motion for page changes.
- Pressed tiles use state-layer opacity and a small compatible shape response.
- Chart changes animate only when the dirty region and hardware budget permit;
  otherwise snap to the new series.
- Reduced motion disables page overshoot and chart interpolation.
- Refresh commit/reject and severe-state transitions use existing semantic
  haptic patterns; ordinary automatic updates do not buzz.

## 12. Scenario and fixture matrix

### Required app-content scenes

- current/partly cloudy baseline;
- hourly/dry baseline;
- daily/mixed conditions;
- details baseline;
- rain starts in 20 minutes;
- clear day;
- clear night;
- heavy rain/thunderstorm;
- snow/freezing condition;
- no minutely forecast available.

### Required lifecycle scenes

- initial cached mount;
- refresh pending;
- current update;
- stale 12 minutes;
- offline with cache;
- error with cache;
- error without cache;
- recovered with higher revision;
- duplicate/regressing revision rejected;
- malformed provider payload rejected.

### Required layout extremes

- `-9°`, `105°`, and a three-digit feels-like value;
- 0% and 100% precipitation/humidity;
- long location name;
- long condition name;
- metric and imperial units;
- midnight/noon time formatting;
- missing optional metric;
- font scale 1.0 and 1.3;
- reduced motion;
- ambient/monochrome;
- small/large square profile if a second square target is added.

Avoid a full Cartesian product. Keep roughly 15–20 decisive scenes chosen to
cover every component state, data extreme, and failure path.

## 13. Conformance and test assets

Generate before app signoff:

- icon gallery AppSpec and golden screenshots;
- typography specimen at all Weather roles;
- Weather component catalog page/state set;
- one semantic scenario file per decisive scene;
- reference Compose screenshots at 240×240;
- LVGL RGB565 screenshots;
- side-by-side, difference, and component-boundary overlays;
- semantic/accessibility trees;
- interaction recordings for page swipe, refresh, stale retry, and recovery;
- RGB565 contrast report;
- touch-target report;
- font codepoint-coverage report;
- provider v2 canonical byte fixtures and invalid corpus;
- frame-time, dirty-pixel, object-count, internal RAM, PSRAM, and flash report.

Quality gates:

- no undocumented interactive target below 48dp (60px at the 1.25 density
  reference profile);
- no clipped content at font scale 1.0;
- an intentional scroll/adaptive layout at 1.3;
- no visible text below the approved minimum role;
- no missing glyphs or tofu;
- no color-only freshness/error communication;
- all semantic values and reading order agree across renderers;
- structured bounds are equivalent;
- all color-pair checks pass after RGB565 quantization;
- full-screen transition p95 at or below 33.3ms on hardware;
- local feedback targets the shared small-region budget when dirty area permits;
- no unbounded allocation and no full-screen ARGB canvas for the chart.

## 14. Gaps visible in the generated concepts

The concepts are strong direction, but not directly shippable at 240×240.

### Touch targets

The bottom action pills downsample to roughly 25–32 physical pixels in several
concepts. At the configured 200dpi Wear reference profile, a normal 48dp target
is 60px. The implemented layouts must reserve larger hit regions, possibly
with a visual pill inside a 48dp semantic/touch container.

### Microtext

Hourly and rain charts contain tiny axis and time labels that look good in the
1254px source but become marginal at 240px. Remove redundant axes, keep at most
four time labels, and rely on the semantic summary for exact interpretation.

### Density

Current conditions can retain the full hierarchy. Hourly forecast must choose
between a large chart and four rich forecast cards; both cannot keep mockup
scale plus compliant touch targets. Start with a shorter chart and four
noninteractive tiles, or show three tiles with horizontal paging.

### Navigation

The PNGs show destination buttons but not a complete way back. A semantic
horizontal pager or equivalent page selector is required before final signoff.

### Refresh semantics

Removing the ugly refresh disc solves the visual issue, but the app still
needs discoverable manual retry. The tappable freshness chip plus promoted
error-state action closes that gap.

### Typography parity

The generated concepts use one coherent sans-serif family. Current LVGL mixes
Roboto display numerals with Montserrat labels. Dedicated/shared Roboto role
subsets are needed for the same typographic voice.

### Real data

The concepts assume coherent hourly, daily, metric, and minutely values. The
current provider cannot supply them and the current host is only a three-state
fixture. Provider v2 is the largest functional prerequisite.

## 15. Recommended build sequence

### Phase 0 — lock the 240px design

- [x] Produce a deterministic v1 recomposition over a 192dp grid with 60px
  normal hit targets. See
  [`reference/inspiration/weather/recomposed-concepts/`](../reference/inspiration/weather/recomposed-concepts/README.md).
- [x] Remove redundant chart microtext in the v1 composition; retain only four
  timeline labels.
- [ ] Approve three versus four hourly tiles. V1 retains four noninteractive
  tiles, with a three-tile fallback to test after real font integration.
- [x] Confirm pager/navigation behavior: left/right swipes and the visible
  advance actions drive the same bounded route state, with no title bar.
- [ ] Confirm freshness/retry behavior.
- [ ] Approve Weather color, type, spacing, and shape token sheet.

First-pass status (2026-08-01): all five concepts now have explicit 240×240px
geometry and inspectable grid/touch overlays. This is an implementation input,
not Phase 0 visual approval; large-font behavior, pager semantics, real font
metrics, and final tokens remain open.

### Phase 1 — generate foundations

- [x] Pin and vendor a curated Meteocons Flat condition set and Material
  Symbols Rounded utility set: 16 condition codes and 17 utility glyphs.
- [x] Generate Android drawables, Compose/LVGL catalog metadata, 64px source
  rasters, 33 review SVGs, and a shared-source gallery golden. Phase 2 added
  size-specific RGB565A8/A8 LVGL assets and a real renderer side-by-side.
- [x] Generate eleven Roboto Weather font subsets for normal and bounded 1.3x
  profiles, with automated provenance, symbol, hash, and codepoint checks.
- [x] Compile the 30-role Weather semantic color scheme to RGB888/RGB565 and
  require all declared contrast pairs to pass after quantization.
- [x] Define ten square Weather shape roles, including asymmetric metric tiles
  and an explicitly experimental cut-corner role.

Foundation status (2026-08-01): canonical source, generated renderer data,
Roboto font assets, contrast report, gallery, and compile/check tests are in
[`reference/weather-foundations/`](../reference/weather-foundations/README.md).
The Weather token sheet is implemented but remains subject to Phase 0 visual
approval; this status does not approve Metric C's LVGL cost or large-font
screen behavior.

### Phase 2 — complete reusable components

- [x] Add semantic AppSpec `icon`.
- [x] Add composable tonal `surface`/child-card.
- [x] Add bounded semantic line/bar chart and atomic series patch.
- [x] Render generic button icons.
- [x] Fix deterministic equal-width rows.
- [x] Add catalog and dual-renderer tests for every primitive.
- [x] Expose horizontal pager after individual screens are stable.

Phase 2 status (2026-08-01): the public contract, canonical CBOR decoder,
SceneSnapshot projection, Rust CommandBatch encoder, Wear Compose renderer,
LVGL renderer, and cross-renderer Weather fixture all implement the new
primitives. Root-level `pageChanged` gestures now survive the bounded AppSpec
split and emit the same signed page delta in Compose and LVGL. The checked
capture is
[`reference/weather-foundations/generated/phase2/weather-side-by-side.png`](../reference/weather-foundations/generated/phase2/weather-side-by-side.png).

### Phase 3 — define real data

- [x] Specify `weather.snapshot.v2` in CDDL.
- [x] Add strict Rust decoder and C/C++ encoder.
- [x] Add worst-case size and malformed-input tests.
- [x] Create deterministic baseline, rain, extreme, stale, and error fixtures.
- [x] Update desktop and firmware mock providers together.

Phase 3 status (2026-08-01): the canonical 13-field payload, fixed-capacity
Rust decoder, shared C/C++ encoder, five deterministic fixture sources, and
native-host v2 delivery path are implemented. Full baseline and rain payloads
encode to 189 and 203 bytes respectively, well below the 512-byte provider
limit. Desktop and firmware deterministic providers now build the same bounded
snapshot through the shared encoder; the real network/location provider
remains a Phase 6 item.

### Phase 4 — build the oracle first

- [x] Build Current in Wear Compose using real Material components.
- [x] Build Hourly and the chart.
- [x] Build Daily rows.
- [x] Build Details metric grid.
- [x] Build imminent-rain variant.
- [x] Capture 240×240, large-font, state, and semantic goldens.

Phase 4 Current status (2026-08-01): the exact-grid Current Conditions scene
now renders from the shared 32-node AppSpec in Wear Compose and LVGL. Both
renderers pass the 48dp touch and visible-bounds gates; the checked resting
comparison is in
[`reference/weather-foundations/generated/phase4-current/report/`](../reference/weather-foundations/generated/phase4-current/report/).

Phase 4 five-screen status (2026-08-01): Current, Hourly, Daily, Details, and
imminent rain now render from the same accepted semantic scenes in Wear
Compose and LVGL. The settled 240×240 batch passes every visible-bounds and
48dp touch-target gate. After the reference-grounded optical pass, the current
live-data captures differ on 10.06%–13.61% of pixels. Current and Hourly are
10.89% and 13.45%; imminent rain is now 10.73% after matching Compose's
fractional grid, physical-pixel histogram arithmetic, and inset-action
placement. Remaining deltas are primarily font rasterization, RGB565
quantization, and structured evidence bounds. The complete normal-size report
is in
[`reference/weather-foundations/generated/phase5-live-data/report/`](../reference/weather-foundations/generated/phase5-live-data/report/).

The intentional 1.3x profile is also complete for all five screens in
[`reference/weather-foundations/generated/phase5-large-font/report/`](../reference/weather-foundations/generated/phase5-large-font/report/).
It scales supporting and body roles while capping the 68px hero and adapting
Daily rows, the Details metric grid, and the imminent-rain headline to preserve
hierarchy at 240×240. All ten normal/large captures pass visible-bounds and
48dp touch-target gates.

### Phase 5 — build the Wasm app and LVGL renderer

- [x] Replace the old single-screen Weather AppSpec.
- [x] Implement fixed-capacity v2 state and formatters in Wasm.
- [x] Implement bounded route navigation for all five compositions.
- [x] Implement the same semantic compositions in LVGL.
- [x] Preserve loading/stale/offline/recovery behavior.
- [ ] Run the complete Parallax comparison and fix structured mismatches.

The visual master is intentionally split into five mountable AppSpecs because
the combined document exceeds the device's 4,096-byte canonical AppSpec
limit. Current is 1,292 bytes, Hourly 2,103, Daily 1,813, Details 1,877, and
Rain 1,164. `tools/weather_foundations/generate_appspecs.py` owns the split;
the Wasm app mounts those bounded routes through real semantic actions. Each
route also carries a root `pageChanged` action, so direct horizontal swipes and
the visible advance targets use one guest-owned navigation state machine.

Phase 5 live-data status (2026-08-01): the Wasm guest now copies provider v2
into fixed-capacity current/hour/day storage and formats every visible value
for Current, Hourly, Daily, Details, and imminent rain. The shared atomic
CommandBatch protocol now patches chart samples and icon identity as well as
text, so condition changes do not require route remounts or renderer-local
state. The native-host flow asserts all route values plus rain-preview retry
and offline recovery, and its nine accepted scene operations replay in a fresh
process with zero Wasm calls. The checked five-case live-data report is in
[`reference/weather-foundations/generated/phase5-live-data/report/`](../reference/weather-foundations/generated/phase5-live-data/report/): all cases
pass visible-bounds and 48dp target gates, with 10.06%–13.61% changed pixels.
Forward/back swipe navigation, boundary no-ops, and the imminent-rain branch
are covered by native-host guest tests; the Compose renderer uses the same
48dp horizontal-drag threshold and signed event payload.
The latest optical pass paints the UV metric tile as a true four-corner
chamfer rather than a nearly square rounded rectangle, uses a single correctly
scaled source raster for the expressive Details glyphs, and keeps Rain action
glyphs legible at physical display scale. Each shipping 18/24/32/64px icon is
now rasterized directly from the pinned Meteocons or Material Symbols vector
instead of being downsampled from the 64px review PNG. Fixed Weather text boxes
also reproduce Compose's font leading, and Hourly forecast glyphs reproduce
the reference's 2.05x optical transform. Across Current, Hourly, Daily,
Details, and Rain this moved changed pixels from
14.61/13.78/10.61/14.56/18.68% to
10.89/13.45/10.06/13.61/10.73%; MAE is now
10.14/10.11/9.00/10.34/8.29. Rain's structured mismatch count also fell from
35 to 17. Its compact visual action is now explicitly floating inside the
unchanged 48dp semantic target, and its chart repeats Compose's centered-cell
bar calculation in physical pixels so 1.25x density cannot accumulate
rounding drift. The same correction improves the rain-state capture to
13.60% / 12.06 MAE and the 1.3x Rain capture to 14.89% / 15.77 MAE.
Swipe-mounted LVGL routes also enter with a bounded 220ms directional ease-out
and 160ms fade without retaining a second AppSpec tree.
Remaining structured mismatches are primarily cross-toolkit measured bounds
and token evidence. Baseline, extreme heat, imminent rain, stale, and error
state goldens now live in
[`reference/weather-foundations/generated/phase5-states/report/`](../reference/weather-foundations/generated/phase5-states/report/),
and atomic semantic label/value patches keep those accessibility snapshots in
step with Wasm-rendered data. The extreme-state pass also protects three-digit
temperature layout. The bounded 1.3x suite is complete; physical hardware
measurements and the real provider are now the remaining milestones.

Phase 5 optical-polish status (2026-08-01): the three-way concept review now
drives explicit geometry rather than serving only as inspiration. Current and
rain hero art are larger, the imminent-rain histogram has the concept's
vertical emphasis, and Current, Hourly, and Rain use compact visual actions
inside unchanged 48dp interactive bounds. The current concept/Compose/LVGL
sheet is
[`reference/weather-foundations/generated/phase5-live-data/concept-compose-lvgl-contact-sheet.png`](../reference/weather-foundations/generated/phase5-live-data/concept-compose-lvgl-contact-sheet.png).

### Phase 6 — hardware and real provider

- [ ] Measure CoreS3/T-Watch frame time, invalidation, objects, RAM/PSRAM, and
  flash.
- [ ] Tune chart/page motion or invoke reduced fallback if budgets fail.
- [x] Implement desktop host-owned geocoding/weather API/cache pipeline and
  replay it through the real Wasm guest and LVGL renderer.
- [x] Implement the ESP-IDF Wi-Fi/location/NVS adapter behind the same bounded
  provider-v2 snapshot.
- [ ] Run disconnect, stale-cache, time-zone, unit, and long-duration tests.
- [ ] Record final Weather parity review and accepted decisive flow.

Phase 6 preflight (2026-08-01): the complete Weather firmware now links for
ESP32-S3. A hardware build exposed the canvas display-list framebuffer as a
permanent 115,200-byte internal-DRAM allocation; it is now allocated lazily
from PSRAM only when a canvas app is rendered. With both normal and 1.3x
Weather fonts linked, the current Weather image is 1,706,976 bytes and leaves 46% of
the 3 MiB app partition free.

Phase 6 CoreS3 checkpoint (2026-08-02): the accepted Current route renders on
the physical 240×240 display with 31 LVGL objects and no stationary redraws.
Lifetime first-paint telemetry records four frames, 14 flushes, 116,655 pixels,
a 45.2 ms mean / 86.3 ms maximum render, and a 3.66 ms mean / 4.21 ms maximum
flush after the direct-vector/line-leading optical pass. Moving the LVGL draw
strips and private WAMR heap to PSRAM improved
steady internal free memory from 26,107 B to 125,883 B and the historical floor
from 12,528 B to 85,548 B; the largest-block floor is 49,152 B. A PSRAM pthread
stack experiment caused a cache-safety boot assertion and was rejected. The
checked configuration keeps task stacks internal. Evidence and calibration
caveats live in
[`reference/weather-foundations/generated/phase6-hardware/`](../reference/weather-foundations/generated/phase6-hardware/).
Touch/route-transition profiling, power, and T-Watch S3 remain outstanding;
the first-paint mean also exceeds the 33.3 ms transition target and needs
profiling or the planned reduced-motion fallback.

Phase 6 real-data checkpoint (2026-08-01): `tools/weather_provider/` now
resolves a configured city through Open-Meteo, requests explicit units and the
resolved timezone, maps WMO conditions into the checked Meteocons vocabulary,
normalizes current/hour/day/minutely data into the existing bounded v2 schema,
and atomically persists the last-good snapshot. Current, stale, and offline
cache states are unit-tested. `tools/fetch_weather.py --render` delivers the
190-byte live San Francisco snapshot through the actual Weather Wasm guest and
captures Current, Hourly, Daily, and Details from LVGL. That live pass exposed
and fixed static Hourly card labels: hour text now updates atomically with each
provider temperature and condition icon. Live captures remain untracked under
`target/weather-provider/`; deterministic visual fixtures remain the oracle.

Phase 6 device-provider checkpoint (2026-08-01): the ESP-IDF host now has an
opt-in asynchronous Weather adapter in `firmware/main/src/weather_provider.cpp`.
It keeps Wi-Fi and HTTPS outside Wasm, requests Open-Meteo with explicit
coordinates, timezone, and unit settings, strictly normalizes the response
into the existing fixed-capacity provider-v2 structure, interpolates the five
15-minute precipitation points into the 13-point five-minute chart, and
persists a versioned last-good NVS record. A failed fetch delivers either that
offline record with an SNTP-derived cache age or a bounded error snapshot.
The default firmware continues to use deterministic fixtures and contains no
credentials.

Enable the adapter with `idf.py -C firmware menuconfig` under **Doodad Weather
provider**, then set the SSID, password, display location, latitude, longitude,
timezone, and units. The enabled configuration compiles as a 2,095,072-byte
ESP32-S3 image and leaves 33% of the 3 MiB app partition free. Its
platform-neutral condition/date/precipitation model and the provider-v2
encoder are covered by native tests. This checkpoint proves both disabled and
enabled links; an SSID was deliberately not copied from the development Mac,
so fresh-fetch, disconnect/cache recovery, and long-duration physical trials
remain explicit unchecked work above.

## 16. Ready-to-build gate

Do not start app assembly until all of the following are true:

- [x] final 240px anatomy for all five concepts is approved;
- [x] icon registry and cross-renderer gallery are checked in;
- [x] Weather typography renders every required string in LVGL;
- [x] Weather color roles pass RGB565 contrast checks;
- [x] icon, composable surface, chart, button icon, and equal-row components
  pass catalog tests;
- [x] provider v2 has a canonical fixture and strict decoders;
- [x] at least one real v2 snapshot renders in the component lab without
  app-specific hard-coded strings;
- [x] touch targets and text sizes pass at true 240×240;
- [x] AppSpec, CommandBatch, Wasm module, object, and memory estimates fit the
  published bounds.

Once those gates pass, building the app is mostly composition and state
handling rather than inventing framework behavior inside a product screen.

## 17. Repository references

- Visual references: `reference/inspiration/weather/README.md`
- Generated concepts: `reference/inspiration/weather/generated-mockups/`
- Current guest: `apps/weather/`
- Current provider contract: `contracts/provider-event-v1.cddl`
- Current SDK decoder: `sdk/rust/doodad-sdk/src/lib.rs`
- Current LVGL Weather pattern: `components/m3e_lvgl/src/appspec/renderer.cpp`
- Current Compose Weather pattern:
  `reference/android-wear/app/src/main/java/dev/doodad/reference/ui/AppSpecReferenceRenderer.kt`
- Component status: `component-matrix.yaml`
- AppSpec policy and limits: `docs/appspec-v1.md`
- Provider architecture: `docs/provider-contracts.md`
- Existing Weather parity evidence: `reference/reviews/weather.md`
- Rendering/performance budgets:
  `docs/material3-expressive-lvgl-implementation-plan.md`
