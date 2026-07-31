# Smart Home parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a favorite living-room
light, preserve the same 72% brightness through a rejected command and retry,
and require an explicit confirmation before unlocking the front door. All six
accepted checkpoints have the same seven semantic nodes and exact normalized
bounds. Pixel identity is not required because Compose and LVGL use different
font rasterizers, RGB565 quantization, and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Home/room context, one
dominant device value, compact status/progress detail, and two decisive actions
own the full 240×240 framebuffer. All visible labels fit in both renderers.

## Source material

Google's first-party Pixel Feature Drop image, Apple's first-party Home status
and light-control images, source URLs, and file hashes are in
[`reference/inspiration/smart-home/README.md`](../inspiration/smart-home/README.md).
They are research inputs only and are not shipped as product assets. Google's
current Home-on-Wear help separately confirms favorite-device access, light
brightness controls, and supported lock/unlock actions.

The oracle borrows the references' information hierarchy rather than their
branding or geometry: favorite device and value first, state detail second,
with failures, rollback, and hazardous confirmation made explicit.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Favorite | `db6958f2834d872b998aea93a51ad33078d058a4ccbfa4b0b160bc0f18259270` | `f8f25f699686fc7b95cd362d0a66cb2235fe9e31296b804c34982f09591116f8` | Living-room light on at 72%, warm white, Light and Door actions |
| Light detail | `81b6a2be375cad18c03db487a4a2dac29868670e508b57ed7904154ad79884ff` | `57d0fa8befad0930eb18560987eda689882c227fd6f81ae7c5847442f1545a5d` | 72% light detail with Turn off and Door actions |
| Rollback | `31bf22444d5129086aa8888e252ef543b832dd4c28b4e4b8f3ca1860ecddd028` | `9b22154c29f765a2dd2cd8dbfa426b60284a844ce98fba469ee57cb81f31dd35` | Provider rejection, restored 72% value, Retry and Door actions |
| Retry | `81b6a2be375cad18c03db487a4a2dac29868670e508b57ed7904154ad79884ff` | `57d0fa8befad0930eb18560987eda689882c227fd6f81ae7c5847442f1545a5d` | Deterministic return to the unchanged light detail |
| Unlock review | `96cd517f74e0313650849198a9496e7496b402fcd769aefc0e893c8f02f7940a` | `16db2e1720eaa5f328f149c72693053cc377186aebff9da8d3705a97ae920bbd` | Identity-aware secure action with Unlock and Cancel |
| Unlocked | `5368c56cadff13a32a23557c49331f3b3056ef82a78fb290a4e95d1a8fd4a357` | `33b0ca7593c20ab539d2574e59cdc883c631355d832db935cf05c76b7ef395b8` | Audit acknowledgement with Lock and Home recovery actions |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render smart-home
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,885 of 57,600 pixels changed (13.6892%)
- MAE 15.3346; RMSE 49.0817

Light, rollback, confirmation, and unlocked states also have exact structure
and bounds with no quality findings. Their changed-pixel fractions are
13.0260%, 13.1389%, 15.2899%, and 16.2969%, with MAE 14.5040, 14.1721,
17.4970, and 19.7697 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and unlock-confirmation images.

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
[`smart-home.resting.watch_square_240.png`](../android-wear/captures/runtime/smart-home.resting.watch_square_240.png),
SHA-256
`9e8de4b1b60c7fb3e3ff172e41de973daaeddcd4b8d6fd44d2ca0e50b6ec11ef`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Smart Home app ID.
- Wear uses Material 3 text, live card, progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The first comparison exposed long-label truncation in LVGL. Tight watch copy
  restored full legibility while preserving the same state and action IDs.
- The shared 32px LVGL live-action font now includes a real `%` glyph with a
  native regression test.
- Home commands, identity, device state, provider rollback, and audit storage
  remain deterministic fixtures. Actual home ecosystems and credentials remain
  host/service concerns.
