# Sleep parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a compact last-night
summary and present the same overnight, morning-summary, stage, and
seven-night states. All five accepted checkpoints have the same seven
semantic nodes and exact normalized bounds. Pixel identity is not required
because Compose and LVGL use different font rasterizers, RGB565 quantization,
and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Context, one dominant
duration, a compact score or stage card, and two decisive actions own the
full 240×240 framebuffer. All visible labels fit in both renderers.

## Source material

Google's official Pixel Watch/Fitbit sleep image and Apple's official score
and history images, with provenance and hashes, are in
[`reference/inspiration/sleep/README.md`](../inspiration/sleep/README.md).
They are research inputs only and are not shipped as product assets.

The oracle borrows the references' hierarchy rather than their geometry:
dominant sleep duration, high-emphasis score or stage information, a quiet
tracking state, and a separate history path. The Doodad fixture makes no
medical claim.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Last night | `c840b1ba2bbaaf640a81d3da8bb82848233c62bd2c7962bbfcecaefb78ab4361` | `c0804fd47ad77d92aa73d04e8a6ec2dd857c35c0308569f16e8718564e2c6888` | 7:42, score 82, stage summary, Start, and Stages |
| Overnight | `3953fccd8180c2d42b784118e90ccf240a12121aa84d6a158583a160a15226fb` | `a9c449e3187fb472942bf76a835ab7d1e5c36bde1b2f1f8f83387b4386c03f06` | 6:18, ambient context, smart alarm, Morning, and Wake |
| Morning | `44e4e26d4cfbd2d4e04a26eb960ba8dfb2694833fb957eb08ab0f5ed8692fff8` | `195c91523b338762568f195eb8bb5535b729bffb6f075a3a391b759f11136e60` | 7:42, high score, restful-night detail, Stages, and Again |
| Stages | `4e3d1f3e9f6dffa3f0337a599eda42ee9ce20268b36633d2310c20cbcb4b7dd5` | `64d8eea48985f415c75f19331e52d383d113dd2dbdf2a2dee8504aaabe1d50bb` | 1:36 deep sleep, 24%, Light/REM detail, History, and Back |
| History | `2264490e78e76018b6defb9ed4119fc4ca68ff6ec783cb60f288bf0d6af1212e` | `1f646dfd21b9b2d5ad5f52538e4c81acd959c866835851b8a2b64d46a60f78a5` | 7:28 average, target status, Night, and Home |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render sleep
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,381 of 57,600 pixels changed (12.8142%)
- MAE 13.8229; RMSE 46.3022

Overnight, morning, stages, and history also have exact structure and bounds
with no quality findings. Their changed-pixel fractions are 14.7396%,
14.0625%, 11.8351%, and 13.9983%, with MAE 16.9400, 15.5145, 12.9366, and
15.2934 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and stage-detail images.

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
[`sleep.resting.watch_square_240.png`](../android-wear/captures/runtime/sleep.resting.watch_square_240.png),
SHA-256
`3e158859e46ac616aa85401ce9ddd8a488a3339f6b389a5619ae96abff1aa6bb`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Sleep app ID.
- Wear uses Material 3 text, live card, progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The unsupported block-character chart was removed. Stage information now
  uses a bounded textual metric and progress value that both renderers and the
  watch font can represent faithfully.
- Overnight data remains a deterministic app state. Actual low-power sensing,
  ambient lifecycle, and bedtime-mode policy remain host/system concerns.
