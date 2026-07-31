# Calendar parity review

Reviewed 2026-07-30 against the square Wear OS 7 oracle and the production
LVGL renderer.

## Disposition

**Equivalent.** Both renderers launch directly into the day agenda and present
the same date context, event order, full-row event target, RSVP decision,
time-zone view, and recovery state. Pixel identity is not required because
Compose and LVGL use different font rasterizers and edge-antialiasing paths.

The app intentionally has no launched-app title bar. Calendar content owns the
full 240×240 framebuffer.

## Source material

The first-party Wear OS and Apple Watch references, provenance, hashes, and
design observations are in
[`reference/inspiration/calendar/README.md`](../inspiration/calendar/README.md).
The images are research inputs only and are not shipped as product assets.

## Accepted decisive flow

| State | SceneSnapshot SHA-256 | LVGL RGB565 SHA-256 | Expected presentation |
|---|---|---|---|
| Today agenda | `72186e74fd41362d5698f668881f03e9358a7c8221354ccdcb87f8b8318e5e8e` | `53a473b0b77cfbe16af1b63aa96f0d6e2ea7889a91c6882da616b93a09202830` | Date context, two event cards, and travel action |
| Event detail | `ac9c7e69a8ce85628635c7556d4610f72a097916019af063ed616b9393399110` | `71beed0c18bcd22197f69d59b86a106bc260b3f1bc83e878e37025056d46bf37` | Time, place, guest count, and RSVP action |
| RSVP confirmed | `d7b600409573b317570d935ee10b519b1d914ab357df6e6c3526369ea75fbbb0` | `6dcdcebbf3041ba5ec35b8fc6a7d7cf04370cc7c4c3b59c50ce31c0dc9a9e5a0` | Going state and offline-save status |
| Travel time zone | `eaf29dd88a3f3bcb00b6c6d624a7154f03d04a06339bfcc6652044f296e00d9d` | `a50b1536358e0e655d5a26dfd383825d8b21011b43cf70a55233e497187773d1` | Local time, time-zone context, and reconnect action |
| Recovered | `d7b600409573b317570d935ee10b519b1d914ab357df6e6c3526369ea75fbbb0` | `6dcdcebbf3041ba5ec35b8fc6a7d7cf04370cc7c4c3b59c50ce31c0dc9a9e5a0` | Return to the content-addressed confirmed state |

Every checkpoint replays without invoking Wasm and attests to the recorded
snapshot, semantic tree, and product framebuffer.

## Comparisons

Resting command:

```bash
./doodad perfect-render calendar \
  --profile watch_square_240 \
  --output target/parallax/calendar-final
```

Resting result:

- 7 reference nodes, 7 product nodes, 7 compared nodes
- 0 structured mismatches; normalized bounds are exact
- 0 quality findings in either renderer
- 48dp minimum touch target in both renderers
- 8,482 of 57,600 pixels changed (14.7257%)
- MAE 15.7975; RMSE 48.7032

Event detail, RSVP confirmation, and travel time-zone states also have exact
seven-node structure and bounds with no quality findings. Their changed-pixel
fractions are 13.5660%, 13.9722%, and 14.1163%, with MAE 13.8694, 14.2591,
and 14.7396 respectively.

The remaining raster delta is concentrated in font shaping, RGB565
quantization, and antialiasing around curved Material surfaces. It does not
change hierarchy, emphasis, state communication, legibility, or touchability.

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
`7a7157799aa755c1264d6bfd1ec613886b70838eb932173492612f096426d5cd`.
The capture also records the accessibility tree, build fingerprint, renderer
build hash, exact snapshot hash, API level, framebuffer geometry, and emulator
revision in its generated runtime manifest.

## Implementation notes

- Pattern selection is structural: one scroll region containing one semantic
  column, one context label, and either two events plus one action or one event
  plus two actions. It is not keyed to the Calendar app ID.
- Wear uses real Material 3 `Card`, `Button`, and `FilledTonalButton`
  implementations. LVGL maps the same roles and bounds through the shared
  component factory.
- The event card itself carries the semantic tap action; the app does not add
  a redundant “open event” screen or button.
- The square adaptation preserves the wrist-first agenda and RSVP model from
  the references while using all available space without a generic app
  header.
