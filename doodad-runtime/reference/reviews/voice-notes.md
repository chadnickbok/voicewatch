# Voice Notes parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a record-ready surface and
show the same elapsed recording, local-safe capture, transcript review, and
saved/sync states. Pixel identity is not required because Compose and LVGL use
different font rasterizers, RGB565 quantization, and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Compact state labels and
content own the full 240×240 framebuffer.

## Source material

The first-party Pixel Watch, Wear OS, and Apple Watch references, provenance,
hashes, and design observations are in
[`reference/inspiration/voice-notes/README.md`](../inspiration/voice-notes/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Ready | `9de3a57ee982b657fdd125f317c0173a0fc17f0f96f9ac22bbfad6bb59b0ff44` | `7bd1df9050f2fd4575935df01b8939b706d943bfa93c22b5179ca321cd73eda3` | Large record affordance, short prompt, and most recent note |
| Recording | `9e0c70e65425916ac74d6f781f914b34457ad1417fed53c688687857e9a482e1` | `3557a2471ab1949a43e4cb9e1ff6c74eac43e5f544fcaebb88239cfa37eecb8a` | Elapsed time, local-safety state, finish, and pause |
| Captured | `3aa8542318201eb70adbc5e49c6c483d3af5eed0523d525a74925cecb733611f` | `47bff61ac424d5e75271a41ff51beb3d9c482154ad39fd0297107b3980dfd2bd` | Locally stored duration with text and delete actions |
| Transcript | `f4f369cf15f49a694ac4c2a8b5eb2c47dbbcc3942fb73610c4b41c0efa1f596c` | `ab4cf77ff0c0771d1e2170180d91057aa74e48eebd94709f7c915d71fad623bf` | Transcript count, captured phrase, save, and record-again actions |
| Saved | `e3556577e928b7e19b0c0e787d413acd2c2aa45f1dc338dca883a52b762a4e45` | `1600c0ef26a4b299877a1919d36eee598e664ecf0bed46a340309c7eeaddd0bd` | Saved duration, derived title, sync status, open, and done |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render voice-notes \
  --profile watch_square_240 \
  --output target/parallax/voice-notes-final3
```

Resting result:

- 4 reference nodes, 4 product nodes, 4 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 12,507 of 57,600 pixels changed (21.7135%)
- MAE 25.2972; RMSE 63.6043

Recording, captured, transcript, and saved states also have exact structure
and bounds with no quality findings. Their changed-pixel fractions are
13.0642%, 13.7431%, 13.7587%, and 14.8646%, with MAE 14.6287, 16.0620,
15.9366, and 17.6233 respectively.

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
`ddf49693257318417d5180e10b4fbb3674c663f50bc8489d6ba5949c121f5b3a`.
The capture also records the accessibility tree, build fingerprint, renderer
build hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural. `voice_ready` and `live_action_detail` are
  generic compositions selected without consulting the Voice Notes app ID.
- Wear uses Material 3 `CompactButton`, `Card`, `LinearProgressIndicator`,
  `Button`, and `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- A 32px Roboto subset preserves elapsed-time and note-count typography
  without shipping a full font.
- The record action is a real `voice_orb` AppSpec node and a normal semantic
  WASM event, not a screenshot-only fixture.
- The decisive flow uses the app's audio provider boundary while keeping
  record, local capture, transcript, and save states deterministic.
