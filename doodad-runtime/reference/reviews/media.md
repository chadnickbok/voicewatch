# Media parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a compact now-playing
surface and show the same playing, disconnected, and reconciled states. All
four accepted checkpoints have the same eight semantic nodes and exact
normalized bounds. Pixel identity is not required because Compose and LVGL
use different font rasterizers, RGB565 quantization, and edge-antialiasing
paths.

The app intentionally has no launched-app title bar. Artwork, track identity,
playback state, progress, output status, and the two decisive actions own the
full 240×240 framebuffer.

## Source material

The first-party Google Wear media-control images, Apple Now Playing image,
provenance, and hashes are in
[`reference/inspiration/media/README.md`](../inspiration/media/README.md).
They are research inputs only and are not shipped as product assets. Google's
fixed-height control hierarchy is the behavioral reference; the square oracle
adapts it to the Doodad geometry.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Ready | `08f48335e3be249a50a43edb376e38a264080adcb619b3f3df6b3dd526a61ec3` | `cc0e2fb6e251e98718f871e6f790fa736822f58a57056e80b441cb39d6791171` | Original album art, title, output, progress, Play, and Queue |
| Playing | `3baafb7031cedc314270071d6c750687552d788037b509e2c8d5f50a214c8d61` | `5feff64af26cc76efd2d14c54df5af96840492b3793987ff339b20e4da2b26c2` | Playback time, Pause, and Offline using the same art asset |
| Offline | `09a9a5f02d52ddc51937bfcd98a58c86a11de1d52f50db8eedafc456209a14df` | `7dcfae518aa3d1581ae14ac654fd9927d011291088979602ccc9df7d41feacd8` | Missing-art fallback, cached status, Retry, and Cached |
| Reconciled | `8785ea4664063d8e861e3e1d43aa4c0c7a451f7639a77661f673d8911624e2b9` | `5e27d147a68dc91cd471793bb3a00d7c7a632eb0272c5493a197387dad2854ab` | Recovered playback at the monotonic fixture position |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render media
```

Resting result:

- 8 reference nodes, 8 product nodes, 8 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 11,896 of 57,600 pixels changed (20.6528%)
- MAE 12.8046; RMSE 40.0560

Playing, offline, and reconciled also have exact structure and bounds with no
quality findings. Their changed-pixel fractions are 18.6892%, 28.6962%, and
18.6059%, with MAE 13.8718, 15.6806, and 13.5188 respectively. The offline
delta is intentionally larger because Compose and LVGL independently draw the
missing-image fallback.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and offline comparison images.

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
[`media.resting.watch_square_240.png`](../android-wear/captures/runtime/media.resting.watch_square_240.png),
SHA-256
`fdb9c5c35ce79724fccebeea3b1a0ec04075679a63c3d97748fc98cd08605d60`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Multimedia implementation

- AppSpec 1.2 adds a renderer-neutral `image` leaf with a content-addressed
  asset hash, semantic label, and `cover` or `contain` fit.
- The package manifest declares the exact asset hash, dimensions, encoded
  bytes, decoded bytes, media type, and relative path; staging rejects any
  mismatch.
- The fixture artwork is an original 96×64 synthwave city. Its DIMG resource
  is 12,300 bytes: a 12-byte header followed by RGB565 little-endian pixels.
- Asset SHA-256:
  `2fb9cd65b78719989e685e43a7179cb69f97e1dfb4604ebfad420cfb91d81028`.
- Compose decodes the package asset to an `ImageBitmap`; LVGL validates the
  same DIMG bytes and renders them through a clipped canvas.
- The offline checkpoint requests an unknown all-zero hash, proving a
  deterministic fallback instead of silently depending on bundled UI state.

This image contract is deliberately separate from the planned Snake canvas:
images are declarative package media, while games need a mutable,
renderer-neutral pixel surface driven directly by Wasm.
