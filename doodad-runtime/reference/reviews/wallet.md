# Wallet parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the same boarding summary,
then preserve the full boarding code, unsafe-update rejection, verified-pass
recovery, and issuer review. All six accepted checkpoints have exact
renderer-normalized structure and bounds. Pixel identity is not required
because Compose and LVGL use different text rasterizers, RGB565 quantization,
and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Route, boarding time,
gate, seat, pass status, and two decisive actions own the full 240×240
framebuffer. All visible labels fit in both renderers.

## Source material

Google's first-party Wear Wallet pass, QR, and pass-detail frames plus Apple's
first-party boarding-pass image, with source URLs and hashes, are in
[`reference/inspiration/wallet/README.md`](../inspiration/wallet/README.md).
They are research inputs only and are not shipped as credentials or product
assets.

The oracle borrows the references' hierarchy: boarding time first, route and
gate context second, a separate high-contrast scan surface, and preserved
verified content when an update cannot be trusted.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Boarding ready | `2a5225da6cd8c20cad745349d728c35c9827ec6ef98ed6d9dcc94a6842d127a7` | `0e622c0412d162b11eeefbcc6b5bb42066295c787bee19dfdcad32baf81edc54` | SFO to JFK, 8:10 boarding, gate B12, seat 18A, Pass and Code |
| Boarding code | `71e016217ba7939d631035a4c3d82294f1a6f1d372e4cec62d9db1d61678af03` | `ca09579a71b861116f1539f40c5a5c68db699e25790ae66b6831491df2bab1df` | Large high-contrast code, route/gate context, Done and Test |
| Unsafe update denied | `6b8ad5ffd36a3b8fe6b785ae3ab28f601042d68fe3ae06fdd13271d22925cfea` | `6aa7219b62569d2eee41d57d3be566cee4dfa5674c84c90cd756bb69d186068a` | Invalid signature, preserved verified pass, Safe and Review |
| Verified pass recovered | `762a7a2782ee4584915d9593700274e66529f546e074cdf0c60ad20cce4c2a35` | `27628bd0ac19ebfefb1196c23c1ec2aea42d3eb219cbc4738e5bb6c7b3f90f2f` | Full gate and seat detail with Code and Test |
| Unsafe update repeated | `6b8ad5ffd36a3b8fe6b785ae3ab28f601042d68fe3ae06fdd13271d22925cfea` | `6aa7219b62569d2eee41d57d3be566cee4dfa5674c84c90cd756bb69d186068a` | Deterministically returns to the identical preserved-pass state |
| Issuer review | `1c4b7c4e85afdcce9d30848e771936550787f633bff2e27f96aa39f110d6d78e` | `6a7ef09a6084128b470ab6a510b411e3e625bbae5f3f14e9fe3e988ca5aab9d3` | Mock Air versus Doodad Air issuer mismatch, Reject and Home |

Every checkpoint crosses the Wallet provider boundary during the live Wasm
run. Replay invokes no Wasm and attests to the recorded snapshot, semantic
tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render wallet
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,517 of 57,600 pixels changed (13.0503%)
- MAE 14.9467; RMSE 49.1486

The boarding-code screen has 6 nodes, exact structure and bounds, no quality
findings, and 3,113 changed pixels (5.4045%). The remaining accepted states
also have exact structure/bounds and no quality findings. Their changed-pixel
fractions are 14.4063%, 13.9549%, 14.4063%, and 16.4826%.

Reviewed evidence is preserved in
[`reference/android-wear/captures/diffs`](../android-wear/captures/diffs):
Compose, LVGL, difference, overlay, boundary, and boarding-code comparison
images.

## Real Wear OS evidence

The runtime lane used:

- AVD `Wear_OS_Square`
- Wear OS 7 / API 37
- signed ARM64 Wear system image revision 1
- Android Emulator 37.1.11
- `wm size 240x240`
- `wm density 200` (1.25 density; 192×192dp logical viewport)
- Wear Compose 1.6.2

The accepted API 37 boarding-summary framebuffer is
[`wallet.resting.watch_square_240.png`](../android-wear/captures/runtime/wallet.resting.watch_square_240.png),
SHA-256
`6e6ab36220d287dbf1128b8dac5db20e6918df0bb4407ea642e5907119656973`.
The separately captured API 37 scan surface is
[`wallet-qr.resting.watch_square_240.png`](../android-wear/captures/runtime/wallet-qr.resting.watch_square_240.png),
SHA-256
`90ad11f7ad00000fe73a0af3d1e90dfb87f5219ccac43af4bf20be3d9ffb098e`.
Adjacent manifests record accessibility trees, build fingerprint, renderer APK
hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision.

## Multimedia and implementation notes

- The demo QR is a real, non-production code encoded in a deterministic
  135×135 RGB565LE DIMG payload. The payload SHA-256 is
  `29ee6d97e8928b49fbbaa49c20a439a1930c4d82de2217490abbe5235a798254`.
- The package manifest declares the content-addressed asset. Wear and LVGL
  independently verify and decode the same bytes; neither renderer receives a
  pre-rendered screen.
- Four documents select `live_action_detail` by structure. The code document
  selects the shared `wallet_qr` image/action pattern by structure; neither
  renderer consults the Wallet app ID.
- Wear uses Material 3 text, cards, progress, `Button`, and
  `FilledTonalButton` components around a native `Image`.
- LVGL uses the shared component factory around a clipped native canvas
  rendering the same RGB565 payload.
- The first comparison exposed a long rejected-update context label. Shorter
  wrist copy restored full legibility without changing domain state or action
  identity.
