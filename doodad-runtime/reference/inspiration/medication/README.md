# Medication reference provenance

Captured 2026-07-30 for the Project Parallax Medication oracle.

## First-party visual references

| File | Source | SHA-256 |
| --- | --- | --- |
| `apple-watch-medications-list.png` | [Apple Watch User Guide: Track your medications](https://support.apple.com/guide/watch/medications-apd3dd24d78b/watchos) | `fd0531bfc2f606e68bd61d7f1bcfbb3a3f78bc2b3b304fcc7ba624d410301565` |
| `apple-watch-medications-logged.png` | [Apple Watch User Guide: Track your medications](https://support.apple.com/guide/watch/medications-apd3dd24d78b/watchos) | `30ee48d2d5e879708312132d7c325755a1935ec268eda3c519a205058fb6bb9c` |

## Behavioral reference

Apple documents a scheduled-medication list, logging all or individual
medications as taken, recording dosage and time, changing a log to taken or
skipped, and an optional follow-up reminder 30 minutes after an unlogged
scheduled dose. Its notification flow also exposes Taken, Skipped, and a
ten-minute reminder:

- [Track your medications on Apple Watch](https://support.apple.com/guide/watch/medications-apd3dd24d78b/watchos)
- [Use the Health app to remind you to take your medications](https://support.apple.com/en-us/105064)

The source explicitly warns that medication tracking is not a substitute for
professional medical judgment. The deterministic Doodad fixture therefore
logs and schedules user-entered data; it does not recommend dosage, timing, or
clinical action.

## Design decisions carried into the oracle

- Medication identity, dose, and due time are visible together.
- Taken is the dominant action; a ten-minute reminder remains one tap away.
- Logging immediately shows the recorded time and preserves an explicit undo.
- Schedule editing and snoozed states are visible, deterministic states rather
  than hidden provider side effects.
- The generic app-title bar is omitted. A compact state/time label lets the
  medication flow own the full square viewport.
- Apple provides the product behavior reference; real Wear Material 3
  Expressive provides the square oracle's components, color, shape, and type.
