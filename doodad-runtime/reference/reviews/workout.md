# Workout parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the active set and present
the same exercise context, weight control, rep target, rest transition,
carried-forward next-set weight, and saved-workout summary. Pixel identity is
not required because Compose and LVGL use different font rasterizers,
RGB565 quantization, and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Workout content owns the
full 240×240 framebuffer.

## Source material

The first-party Wear OS and Apple Watch references, provenance, hashes, and
design observations are in
[`reference/inspiration/workout/README.md`](../inspiration/workout/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
|---|---|---|---|
| Active set | `7939ab71e4e9d70cbbd6c78e6233391d01fedaa0b03b0c423a4f724f5a6d275e` | `9650b95b38686fc6e8c695508e907a8c12a9590f02f2cd974cf2329029d6a1e4` | Set context, 135 lb control, target card, and Log set action |
| Rest | `95fa60492db6113e4cf6e2124755f3e7c90464de2f40ac3c506e038a778be6ba` | `c4b73172df156da987a2588da6402b22ea8d6de1d232a4269dee5f9203b26b7a` | One-minute timer, recorded set, next set, and end actions |
| Next set committed | `c9bcbd6dc329519fea44afb93411f15feda7c4f73b03038fa728a925171a6589` | `83900d60ad08b38c4a1b10eb3b0cf4e892d9719aaf8161eef6bba793d730a634` | Set 4 with the committed 145 lb weight carried forward |
| Summary | `d817e8ea4e96d53f40583f9fdd4009c3fd82be165d68f19ac759883d4e2572f8` | `6ba2650e1d59f30e5e3ce097e56937e17f9c48606a3924d15c10518a687a0387` | Set count, training volume, exercise detail, and repeat action |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render workout \
  --profile watch_square_240 \
  --output target/parallax/workout-final
```

Resting result:

- 5 reference nodes, 5 product nodes, 5 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 9,526 of 57,600 pixels changed (16.5382%)
- MAE 18.5227; RMSE 54.1543

Rest, committed next-set, and summary states also have exact structure and
bounds with no quality findings. Their changed-pixel fractions are 9.6580%,
16.6684%, and 31.4392%, with MAE 10.6445, 18.7835, and 18.9933
respectively. The summary's larger changed-pixel fraction is dominated by
slightly different RGB565 surface colors across its three large containers;
the low MAE, exact geometry, and identical hierarchy show that it is not a
layout divergence.

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
`32c53597be4222a310168fb5cd92cb27f57a63cebb9634f066213b99c5acc21b`.
The capture also records the accessibility tree, build fingerprint, renderer
build hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural. Active/next set, rest, and summary each
  have a separate generic composition selected by their component facts, not
  by the Workout app ID.
- Wear uses Material 3 `ButtonGroup`, `CompactButton`, `Card`,
  `LinearProgressIndicator`, `Button`, and `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps the
  Material roles through the shared component factory.
- The 28 px Roboto numeral subset now includes `:` so compact timers cannot
  regress to a missing glyph.
- The square adaptation keeps the set workflow wrist-first and uses all
  available space without a generic app header.
