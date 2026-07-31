# Sensor Recorder parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a ready-to-record sensor
surface and show the same recording, paused, completed-session, and
export-ready states. All five accepted checkpoints have the same seven
semantic nodes and exact normalized bounds. Pixel identity is not required
because Compose and LVGL use different font rasterizers, RGB565 quantization,
and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Session state, the
dominant metric, compact XYZ or persistence detail, progress, and the two
decisive actions own the full 240×240 framebuffer.

## Source material

Google's first-party Health Services Exercise Sample screenshots, ongoing
activity image, provenance, and hashes are in
[`reference/inspiration/sensor-recorder/README.md`](../inspiration/sensor-recorder/README.md).
They are research inputs only and are not shipped as product assets.

The oracle borrows the sample's hierarchy—not its domain claims: live status
and a dominant metric, compact supporting data, explicit pause/finish
controls, and a separate completed-session state. Sensor Recorder continues to
show deterministic raw XYZ samples.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Ready | `6a9f7a3a97f609dc6b29016b83e839201a4b71f309c0a96824a89f28b1bff709` | `22432cce125707c7551766104307b733372ba7600cda5813d61254841d6be841` | 50 HZ, bounded XYZ buffer, Record, and Session |
| Recording | `30f0238d7f527c22db01269a38d246d938c20cd18cdb70c8f5d5e729609ecd77` | `5197e675e20d097288fcac5b0d240faa5783d453e37b1aa9ed0af7ef6699063b` | Elapsed time, 1024 samples, live XYZ fixture, Pause, and Finish |
| Paused | `5d1d98b7de30c7a00e5abe55f39fd4b35e419278d3c4efab034a1e78f07a208a` | `3003000d6f876e0dfb115d67cf9f11f0a6ec3238ea2ddfa43c264365643e7f32` | Stable elapsed time, committed 48 KiB buffer, Resume, and Export |
| Session | `0875e975fda52a56016862d62b3915db9c1fb523ef791544b6bc534c9435875f` | `6a1bece7a3bd9d42fb4211a8f89ff84706f70adc57fab556653abc960362ac07` | Complete sample count, checksum, Export, and Again |
| Export ready | `f51d38f2103451dc4eebfe052897886185a0b531393989e2bce668c491ace918` | `509c7459acb4fd4f599622f9593a952f2dc63fd12b114976fe3efb9769476591` | CSV result, deterministic filename, Session, and Home |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render sensor-recorder
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,667 of 57,600 pixels changed (13.3108%)
- MAE 14.0239; RMSE 46.5003

Recording, paused, session, and export-ready also have exact structure and
bounds with no quality findings. Their changed-pixel fractions are 14.1128%,
14.1042%, 15.6788%, and 14.5295%, with MAE 16.1245, 16.0621, 18.0044, and
16.1627 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and live-recording images.

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
[`sensor-recorder.resting.watch_square_240.png`](../android-wear/captures/runtime/sensor-recorder.resting.watch_square_240.png),
SHA-256
`e5d50777dd1764f8958fde1eff0d3d97dda775f34b36be7d10875e285826aaa3`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Sensor Recorder app ID.
- Wear uses Material 3 text, live card, linear progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The square profile presents metrics and controls together. Google's round
  exercise sample uses a horizontal pager for these two groups.
- The deterministic provider boundary is crossed for start, pause, export,
  and CSV completion; replay never invokes Wasm.
- Background capture and return affordances remain system-owned ongoing
  activity or Wear OS 7 Live Update concerns, not fake in-app chrome.
