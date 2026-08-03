# Calories parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into today's calorie total and
show the same remaining-goal context, progress, latest meal, quick-add flow,
voice review, committed total, and over-goal state. Pixel identity is not
required because Compose and LVGL use different font rasterizers, RGB565
quantization, and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Nutrition content owns
the full 240×240 framebuffer.

## Source material

The first-party Wear OS and Apple Watch references, provenance, hashes, and
design observations are in
[`reference/inspiration/calories/README.md`](../inspiration/calories/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Daily dashboard | `f514ef0b481d622187a7f8c5e1438e63e721f5b70292887e5714dc0540f9b3d1` | `749187465f7588d0fda424436007d1279be23838ac9d6644eba4ed7a6fa469b8` | Remaining goal, 1,420 kcal hero, progress, latest meal, and add action |
| Quick add at 150 | `e29f114a87633471f5bfe8158315569ae6bb490922ae87f1dc36683c1159a012` | `e10371d2c0dc8887d54bbd846de91cfb1b34518f6f7eac4cdc83ec63a97d2398` | Updated amount, daily context, add, and voice actions |
| Committed total | `9828be69ec0c39b8f833b539e65b7b5a11c5cd2a5ecae37b3e6af05fa4f845ae` | `f34ecdefebe112d1ef223706ba2db7e043f6050c01632e6fa298dd5f24e19387` | 1,570 kcal with 430 remaining |
| Voice review | `a53f26701df176c05178f78eb1079c3322f93db69b0617204df4e21a4619b58a` | `654d1498019d57eb0c3608a57038c2b1634a7be2e06bb157c11c5d88f08858ae` | Parsed 650 kcal meal with save and edit actions |
| Over goal | `586aa96ab373bb235c94660dd46300a3a61629d56f41ba0af364b072867b3447` | `f05ee01a9444d8495ea45190f524185e6117d00115c40835f74e0f1f3b6187e3` | 2,220 kcal, explicit 220 over context, and a full progress track |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render calories \
  --profile watch_square_240 \
  --output target/parallax/calories-final
```

Resting result:

- 6 reference nodes, 6 product nodes, 6 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 9,000 of 57,600 pixels changed (15.6250%)
- MAE 16.8540; RMSE 52.4263

Quick-add, committed-total, voice-review, and over-goal states also have exact
structure and bounds with no quality findings. Their changed-pixel fractions
are 16.7014%, 15.1615%, 14.1198%, and 14.2517%, with MAE 17.4677, 16.7991,
15.9099, and 16.8275 respectively.

## Real Wear OS evidence

The runtime lane used:

- AVD `Wear_OS_Square`
- Wear OS 7 / API 37
- signed ARM64 Wear system image revision 1
- Android Emulator 37.1.11
- `wm size 240x240`
- `wm density 200` (1.25 density; 192×192dp logical viewport)
- Wear Compose 1.6.2

The resting emulator framebuffer SHA-256 is
`45d8a7aec268110a11867a6214a637087085a4b4a6e6d2ebd886c9ff8efb173d`.
The capture also records the accessibility tree, build fingerprint, renderer
build hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural. Dashboard, quick add, and voice review are
  three generic compositions selected by their component facts, not by the
  Calories app ID.
- Wear uses Material 3 `LinearProgressIndicator`, `Card`, `CompactButton`,
  `Button`, and `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps the
  Material roles through the shared component factory.
- A 32 px Roboto subset preserves comma-separated calorie numerals without
  shipping a full font.
- WASM value changes now update both the generic stepper text contract and its
  styled LVGL numeral, preventing visual and semantic state from diverging.
- The square adaptation uses all available space without a generic app header.
