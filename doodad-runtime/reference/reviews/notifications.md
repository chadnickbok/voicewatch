# Notifications parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers consume the same AppSpec snapshots and present
the same unread context, sender hierarchy, message previews, quick replies,
dismissal, clear, sent confirmation, and empty state. Pixel identity is not
required because Compose and LVGL use different font rasterizers and
edge-antialiasing paths.

The app intentionally has no launched-app title bar. Notification content owns
the full 240×240 framebuffer.

## Source material

The first-party Wear OS and Apple Watch references, provenance, and design
observations are in
[`reference/inspiration/notifications/README.md`](../inspiration/notifications/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
|---|---|---|---|
| Two unread | `bb62eb1fffa23a6c0fde09a955534f98a3e9b0083e755f43bcd8b2ee12503324` | `4198af9beff51c1bd63782925777e689f166e3f31395bd1f6c12f2c60af503ce` | Two glanceable message cards and `Clear all` |
| Detail | `a786dc0d3fef2822cef83b88a3943d06272d20523d0ef468d01758f7ac20c015` | `d5c90b2e07da0611d89894434b5ad4f2ba95f2c482610fa9e38b236aa5af4642` | Sender context, complete message, `Reply`, `Dismiss`, and `Back` |
| Quick reply | `f6341ac395d378a712a2987490b2d2b75d231e9da14a5cb8bf60be53bffda793` | `5d50674fa59d23f9a43d3dc1c2e6b14d98ee6a0e1b77b3225dd5f0902c3da23b` | Original message retained with `Coming` and `Great` suggestions |
| Sent | `f2b0b38984faff84993607734a6917f4c211251ba451c250f687fac3b72eaf42` | `5934c4f45b88d12da141921b81db3f8545f9d12037e447430bb7ee9d276a5bff` | Exactly-once confirmation and `Done` |
| One unread | `ff55d5bf58d6eed03ab0469204c5bea58c63af989dc595264f7cb9bdfce92890` | `f7dae7662590c03b7723b09907cf61558ccea87cb213da874bfd2f57dc6208ff` | Remaining build notification and `Clear all` |
| Empty | `4e01661ed9ead1cb45ae55e970194b367e2f4ad006b63c043886270c1e7cb30b` | `83560bd603b3b0516c58d2518fc769cb4b5a38832f82901e50cdec83a91d8187` | Centered caught-up state with no phantom action |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Resting comparison

Command:

```bash
./doodad perfect-render notifications \
  --profile watch_square_240 \
  --output target/parallax/notifications-final
```

Measured result:

- 6 reference nodes, 6 product nodes, 6 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 8,510 of 57,600 pixels changed (14.7743%)
- MAE 15.8533; RMSE 48.8438

The visible delta is concentrated in text rasterization, internal card
padding, and antialiasing at rounded edges. It does not change hierarchy,
relative emphasis, state communication, legibility, or touchability.

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
| Two unread | `3b00f26418ab87dadc774b5b0cd7c8a19ca78cd7f443dacf6ec5ed03fdc461ad` |
| Detail | `2de2a2ebea0b0622016144ffa39462206f4b0fa1eee4b7ba3943e77893981300` |
| Quick reply | `f63f53abe1a3c79c09c16a656bde67d4669ebd1822d2e267196af63c9dd23162` |
| Sent | `87f998f29acc9490c9ca0a8f5d8f132541b6d9660f52ed6ac83dfd7a7e719f6e` |
| One unread | `9e19d03b4e7ecb0e17c59e845f126d241e6b81b2cd0c62f6a29483957735b222` |
| Empty | `54d8c41eb0f19309af23c926d0403597417556bb4ddefe22f49005c86f5cfdc7` |

The resting capture also records the accessibility tree, build fingerprint,
renderer build hash, exact snapshot hash, API level, framebuffer geometry,
and emulator revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural (one scroll region containing one or two
  cards and one or three actions), not keyed to the Notifications app ID.
- Message cards are genuinely interactive in both renderers; the semantics do
  not rely on a button-shaped visual substitute.
- Every decisive-state string fits without clipping or missing-glyph boxes.
- The notification fixture remains text-only rather than faking a renderer
  local avatar. Checked-in photos and album art will enter through the shared
  content-addressed `image` component validated by the Media app.
