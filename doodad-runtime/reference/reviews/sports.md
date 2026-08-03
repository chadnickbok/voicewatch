# Sports parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the same live baseball
game, then preserve follow, coalesced score-update, final, and scoring-play
states. All five accepted checkpoints have the same seven semantic nodes and
exact normalized bounds. Pixel identity is not required because Compose and
LVGL use different font rasterizers, RGB565 quantization, and
edge-antialiasing paths.

The app intentionally has no launched-app title bar. Match/inning context, one
dominant score, compact outs/runners or latest-play detail, and two decisive
actions own the full 240×240 framebuffer. All visible labels fit in both
renderers.

## Source material

Google's first-party Wear OS 7 Live Update image, Apple's first-party Sports
Live Activities image, and three real Wear OS baseball app images, with source
URLs and hashes, are in
[`reference/inspiration/sports/README.md`](../inspiration/sports/README.md).
They are research inputs only and are not shipped as product assets.

The oracle borrows the references' information hierarchy rather than their
branding or geometry: score first, inning/outs/runners second, latest scoring
play third, with following and ended status made explicit.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Live game | `0bf81869a12e7f7d1780e4f63f46c463f3fa9d1eab35a103ca3d9ed2de84df6d` | `a5ebd4eb27bfe8402611046997fc5e22ac1fc1dfa8697eae9449a567590a2a06` | SF leads LA 3:2 in the top eighth, one out, runners first and second, Follow and Update |
| Following | `2ebf24542d9e6efc86422bc78acece18d97c845709f34c2e46a1271872e732b5` | `ac08bd04573fbcc71b160cd7a715d5da9431962da77a1ef7ba50f8c195396d90` | Same live state with followed status, Update, and Stop |
| Score update | `27006c12c63e9b992d5e4b393adbfab4832ff4f87c556de42500c75c90322fca` | `756ededf5a83d73c680d014279fe39b9644d80d15095069234b7faac71bf1a7d` | 5:2 after Lee's two-run double, three provider events coalesced, End and Live |
| Final | `1d3e705bfbb4a4b671657346ffbfa40c55029d5fd38e92af644b9021f887d398` | `4567791a390407d26d4b45824224fcd4d08cbe510e606fd5d2e0366798d38e47` | SF wins 5:3, follow activity ended, Plays and Again |
| Scoring play | `4d7765fbb77a1ea9d0bd4dc88116deafa53a6ba34c9747f939e796ce4b4e1360` | `c1b12272e48dff290f8844885b4aaf7d5662167c82b04256f222d55eff60033a` | Five runs across four plays, Lee double highlighted, Final and Home |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render sports
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 6,868 of 57,600 pixels changed (11.9236%)
- MAE 13.3023; RMSE 45.6422

Following, score-update, final, and scoring-play states also have exact
structure and bounds with no quality findings. Their changed-pixel fractions
are 11.9740%, 12.5573%, 11.5156%, and 14.2413%, with MAE 13.5105, 13.8013,
12.2590, and 16.5235 respectively.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and score-update images.

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
[`sports.resting.watch_square_240.png`](../android-wear/captures/runtime/sports.resting.watch_square_240.png),
SHA-256
`e34e25010ee4f82d98ce08ded285dd6139a86ef131876fa5affe256ca2522443`.
The adjacent manifest records the accessibility tree, build fingerprint,
renderer APK hash, exact snapshot hash, API level, framebuffer geometry, and
emulator revision.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Sports app ID.
- Wear uses Material 3 text, live card, progress, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The first comparison exposed a missing middle-dot glyph plus four truncated
  labels. Plain separators and tighter watch copy restored full legibility
  without changing game semantics or action IDs.
- Provider bursts remain deterministically coalesced into the newest score.
  Actual leagues, feeds, logos, live activities, and notification delivery
  remain host/service concerns.
