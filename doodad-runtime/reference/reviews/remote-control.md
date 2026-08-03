# Remote Control parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the same title-free camera
remote, then preserve camera-ready, countdown, disconnected/recovered, and
captured states. All seven accepted checkpoints have the same seven semantic
nodes and exact normalized bounds. Pixel identity is not required because
Compose and LVGL use different text rasterizers, RGB565 quantization, and
edge-antialiasing paths.

The live viewfinder owns 184×120dp of the 192×192dp logical screen. A compact
status pill and dominant zoom/countdown/result pill are overlaid on the image;
two full 48dp actions use the remaining bottom edge. The launched app has no
top bar or redundant app title.

## Source material

Google's Wear OS camera documentation, two real Pixel Watch camera references,
and Apple's first-party Camera Remote image are catalogued with URLs and hashes
in
[`reference/inspiration/remote-control/README.md`](../inspiration/remote-control/README.md).
They are research inputs only and are not shipped as package assets.

The shipped viewfinder is an original image-generation fixture. Its committed
690×460 RGB source is reduced deterministically to a 230×150 physical-pixel
preview, encoded as RGB565 DIMG, and addressed by SHA-256
`777d468ea847318acd22e2eb79f108e75e2674448e8b53fa8fba0bc08fd7b522`.
The same bytes are decoded by Wear Compose and LVGL.

## Accepted decisive flow

| Stage | State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 |
| --- | --- | --- | --- |
| 0 | Phone ready, 1.0X, Camera / Offline | `24fbfc8e6e9f7aa996bf90cc373c78c5c83f86b184ae495f1f53b01e06a99667` | `ea9d7bc5e5ece9009b805a0b49c9af25be76f84f6c157003359e23c6b381ce8a` |
| 1 | Three-second timer, Shutter / Offline | `bffaebc1da06df3bf565c02a78babaf5fbc7edfe448cb009aef8d3b76bc1d5dd` | `d0902e2cd6a6e8b47fbd0664ebf8bed9c89e63a835dd7b2844e0a3fd36a7ede1` |
| 2 | Taking photo, countdown 3, Now / Offline | `5c6451db2abe1778fe8df208ca989c253b8134157d846d5f6d358f669d989b56` | `0148db82379244c18afdddc5a7fd1293381f7ef9c9ace13eb1c9e8cffb3b084f` |
| 3 | Link lost, Offline, Retry / Controls | `42b0dca1a272a456fc7b5977c1b0c9639ec692e319104a850c8b8bb1a6072a98` | `2307491f48377d08fb98c2cd6868ff977d2a81ccf26ec6f6532c6707ff846aee` |
| 4 | Recovered camera timer | `bffaebc1da06df3bf565c02a78babaf5fbc7edfe448cb009aef8d3b76bc1d5dd` | `d0902e2cd6a6e8b47fbd0664ebf8bed9c89e63a835dd7b2844e0a3fd36a7ede1` |
| 5 | Second countdown | `5c6451db2abe1778fe8df208ca989c253b8134157d846d5f6d358f669d989b56` | `0148db82379244c18afdddc5a7fd1293381f7ef9c9ace13eb1c9e8cffb3b084f` |
| 6 | Photo saved, Captured, Again / Controls | `606e86143fdc4293ef0e38710a2dd421a17b3b384e11af8d3b87a45a9d2e4ef7` | `a4adb5af6e3174b3b19b6811cc7dec0836dad433a00308a058f4b5132c3b3784` |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, package asset, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render remote-control
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 9,004 of 57,600 pixels changed (15.6319%)
- MAE 9.9606; RMSE 40.5745

The other accepted checkpoints also have exact structure and bounds with no
quality findings. Their changed-pixel fractions are 15.3958%, 14.8316%,
15.9566%, 15.3958%, 14.8316%, and 17.1944%, with MAE 9.1949, 8.3558,
10.2080, 9.1949, 8.3558, and 13.3484 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and captured-state images.

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
[`remote-control.resting.watch_square_240.png`](../android-wear/captures/runtime/remote-control.resting.watch_square_240.png),
SHA-256
`6038b8c179176765ca41786554b7d3c9c810a345c8027d2681e33ef06b877477`.
The adjacent manifest records the accessibility tree, renderer APK hash, exact
snapshot hash, API level, framebuffer geometry, and emulator revision.

## Implementation notes

- All five documents select `camera_remote` from structure alone; neither
  renderer consults the Remote Control app ID.
- The viewfinder source is decoded and reduced by
  `tools/generate_remote_asset.py` without optional image libraries. Its DIMG
  is 69,012 encoded bytes and 69,000 decoded bytes.
- The 230×150 DIMG maps one-to-one onto the 184×120dp viewfinder at density
  1.25, so the LVGL product path does not need runtime resampling.
- The first comparison exposed a missing lowercase `x` in the bounded large
  display font and a truncated `Reconnect` action. Uppercase state typography
  and the shorter `Retry` label eliminated both defects without changing
  semantics or action IDs.
- Real phone discovery, live camera streaming, zoom gestures, timer
  progression, and capture delivery remain host/service concerns. The
  conformance app deliberately validates their UI states with deterministic
  fixtures.
