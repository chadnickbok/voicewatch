# Tasks parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the actionable list and
present the same list context, whole-row completion targets, add-by-voice
action, inserted task, and checked state. Pixel identity is not required
because Compose and LVGL use different font rasterizers and edge-antialiasing
paths.

The app intentionally has no launched-app title bar. Task content owns the
full 240×240 framebuffer.

## Source material

The first-party Wear OS and Apple Watch references, provenance, hashes, and
design observations are in
[`reference/inspiration/tasks/README.md`](../inspiration/tasks/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
|---|---|---|---|
| Two tasks | `7b9caf9e61d25246a3988d5e95f0f3dd9c76855d4f3e62e68779641897dbe3c7` | `4fefc7a0e172c859d2ebeae8b9f16c00aa169f2bbababddaa3b6ab95158a666d` | Two unchecked rows and a full-width add-by-voice action |
| Task added | `a5b5f2c6d82bd2c9b4ff3c5c7f6d44008006364eb1aaabaf2c1b3b6cb3a3d14c` | `97f470fc38421ab6218ad4a820dd1c7bf7db03bda253bbdbc39df3d5fc58baf2` | Three visible, unchecked task rows |
| Bananas complete | `f2640500bce65f80fe370428a87d4b1c2b0013cf7a8488cf716a9508e8dbe204` | `2931f2e651eea68f44aa58ff43fafcbd0b63d4a6bc0951855494b1d744c148b3` | Checked row receives primary-container emphasis and a checkmark |
| Milk complete | `5d35e8badccfb2f51355c7833c4335246ee3adfdbfd13dfffcea0e8e89b385a2` | `e29a57cfb5ddcc7b19863872a2dc4a91c1c04be5251a12d074b226f22abb9eb8` | A second completion updates in place and the count reaches one |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render tasks \
  --profile watch_square_240 \
  --output target/parallax/tasks-final
```

Resting result:

- 6 reference nodes, 6 product nodes, 6 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 17,359 of 57,600 pixels changed (30.1372%)
- MAE 16.1159; RMSE 42.9049

The added and completed decisive states also have exact six-node structure and
bounds with no quality findings. Their changed-pixel fractions are 9.6406%
and 9.7049%, with MAE 10.2048 and 10.2991 respectively.

The resting changed-pixel fraction is elevated by small color quantization
differences across the large neutral add surface. The error magnitude remains
low and the delta does not change hierarchy, emphasis, state communication,
legibility, or touchability.

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
| Two tasks | `6c2bb019166ef49173572f48695cf52db5363f3e10c2c827e7c87ca9b7c2948e` |
| Task added | `9c8ea41bd9310c3e203440b3d97cd98e6f73622757dacaa663938f8f13e339c4` |
| Bananas complete | `10e5540af7a20d527026b6b71f64c42b34ee12b0d8a04c77a0ffa39c5953ad45` |

The resting capture also records the accessibility tree, build fingerprint,
renderer build hash, exact snapshot hash, API level, framebuffer geometry,
and emulator revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural: one scroll region containing one label and
  either two toggles plus add or three toggles. It is not keyed to the Tasks
  app ID.
- Wear uses the real Material 3 `CheckboxButton`; LVGL maps the same semantic
  node to a checked whole-row control.
- `UiCommandBuffer::set_checked` and the host command reconciler now update
  boolean component state in place, so the WASM guest does not fake completion
  by changing label text.
- The square adaptation preserves the wrist-first interaction model from the
  references while using all available space without a generic app header.
