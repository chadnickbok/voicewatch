# Medication parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into a due-dose surface and
show the same logged, reminder editing, due-after-save, and snoozed states.
Pixel identity is not required because Compose and LVGL use different font
rasterizers, RGB565 quantization, and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Medication identity,
schedule, dose, and the two decisive actions own the full 240×240 framebuffer.

## Source material

The first-party Apple Watch medication images, provenance, hashes, documented
logging behavior, and medical-judgment warning are in
[`reference/inspiration/medication/README.md`](../inspiration/medication/README.md).
The images are research inputs only and are not shipped as product assets.
Wear Material 3 Expressive remains the square styling and component oracle.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
| --- | --- | --- | --- |
| Due | `43f65e5e02a253ea2607378c279c228a0bb462d44ac1c3f672c4c10dbe5326dd` | `fd53bd80c3ae7428c35d75bb2502ffc1a0de2dc34439ebab9d8c46d81f8258e5` | Vitamin D, due time, exact dose, Taken, and ten-minute reminder |
| Logged | `484b9d3ec6a15158a6068e03ab73a73a76171373446cf1ca5760c07b0eba84f7` | `4dac07ae4dfb41883faf0b4631db044f9891f66d81eacfea86202a7025157bb2` | Recorded time, dose, streak, reminder edit, and undo |
| Reminder | `c534d05e21dd7a5e320ee7f5e77f55309a22389967841461a54b0fd5c3bfad20` | `f4e2e6551d38bfc06a34b7917b6f983a11c923f1062f77216820d9a29e015209` | Daily schedule, follow-up status, save, and cancel |
| Due after save | `17f35ef170a71d1395651cd1a8a30bb2ac8d76dd624afefb307a3bd0d0aebfa9` | `370573585ffb554fbca834cd4aad0ff14bd640efb23c6185f7d5b950f9438251` | Saved 8:00 schedule with Taken and ten-minute reminder |
| Snoozed | `4d72ceb7e77d7d4726d44b6731c1e5dd2a80ca39562263952112997aa71fcf21` | `66fa59cc2aab594fee71a377e70ace9aae467b5c387728c8611d7de6095c7681` | New reminder time, bounded progress, Taken, and schedule edit |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render medication \
  --profile watch_square_240 \
  --output target/parallax/medication-final5
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 7,350 of 57,600 pixels changed (12.7604%)
- MAE 14.9777; RMSE 50.5524

Logged, reminder, due-after-save, and snoozed states also have exact structure
and bounds with no quality findings. Their changed-pixel fractions are
14.1823%, 12.7986%, 12.6875%, and 14.5313%, with MAE 16.5983, 14.4929,
14.8778, and 17.0121 respectively.

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
`4873c3561a6090320b70cfec06b7e6f88a59ab552767819b2568b439c9f13da5`.
The capture also records the accessibility tree, build fingerprint, renderer
APK hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision in its generated runtime manifest.

## Implementation notes

- All five documents select `live_action_detail` from structure alone; neither
  renderer consults the Medication app ID.
- Wear uses Material 3 `Card`, `LinearProgressIndicator`, `Button`, and
  `FilledTonalButton` components.
- LVGL gives the same semantic nodes exact normalized bounds and maps Material
  roles through the shared component factory.
- The shared 32px Roboto subset now covers uppercase live-action values as
  well as elapsed-time numerals.
- A renderer bug found during review was fixed: an absent `live_card.progress`
  property no longer inherits the wire default maximum or draws a false
  progress endpoint.
- The decisive flow crosses the app's medication provider boundary for every
  action while keeping schedule and log states deterministic.
- The fixture records user-entered data and never recommends a dose, time, or
  clinical action.
