# Powerlifting concept prompts v2

V2 uses the approved high-resolution Weather Current concept as an edit/style
master. Every result is a single square watch framebuffer. Flow boards are
assembled afterward so image generation cannot distort the device geometry.

## Shared invariants

```text
Use case: style-transfer
Asset type: implementation-facing square smartwatch UI mockup
Input images: Image 1 is the edit target and visual-system master from the
approved Doodad Weather app
Primary request: replace the weather content with the specified powerlifting
screen while preserving the established Doodad visual system
Canvas: exactly square; the image itself is the full 240 x 240 framebuffer
shown at high resolution; no surrounding board, label, arrow, bezel, watch
case, border, shadow, perspective, or environment
Preserve: near-black navy background; restrained deep-blue Material surfaces;
subtle tonal variation; Roboto regular/medium; tabular hero numerals;
off-white primary text; blue-gray secondary text; Weather's spacing rhythm,
corner vocabulary, hierarchy, and full-width inset bottom action
Components: use recognizable Wear Material 3 / shared Doodad primitives for
generic interaction: Card or tonal surface, ListRow, ButtonGroup,
CompactButton, FilledButton, FilledTonalButton, status pill, progress
indicator, and Material Symbols Rounded icons. New workout-domain components
are allowed when required to express a real lifting task; they must inherit the
same tokens and compose cleanly with these primitives. Keep controls flat and
practical.
Workout roles: lavender primary/current; mint success/complete; coral
missed/destructive; amber warmup/plate assistance
Constraints: no generic app title bar; no condensed or display typeface; no
glossy 3D treatment; no neon glow; no thick decorative outlines; no bespoke
athlete illustrations; no circular icon medallions; no tiny spreadsheet;
minimum 48dp semantic actions; one dominant value or action; render only the
specified text verbatim; no logos, trademarks, or watermark
```

## Screens

1. `01-today`: `HEAVY DAY`, `SAT · WEEK 4`, `SQUAT · BENCH · DEADLIFT`,
   `14 SETS`, `READY`, `START WORKOUT`.
2. `02-session`: `HEAVY DAY`, `0 / 14 SETS`, `BACK SQUAT`, `5 SETS`,
   `BENCH PRESS`, `5 SETS`, `DEADLIFT`, `4 SETS`, `BEGIN SQUAT`.
3. `03-exercise-picker`: `CHOOSE EXERCISE`, `RECENT`, `BACK SQUAT`,
   `FRONT SQUAT`, `PAUSED SQUAT`, `+ CUSTOM`.
4. `04-active-set`: `BACK SQUAT`, `SET 3 OF 5`, `140`, `kg`, `× 5`,
   `LAST 137.5 × 5 @8`, `COMPLETE SET`.
5. `05-weight-editor`: `WEIGHT`, `140.0`, `kg`, `−5`, `−2.5`, `+2.5`, `+5`,
   `20 · 20 · 10 · 2.5`, `DONE`.
6. `06-set-result`: `SET 3`, `140 kg`, `REPS`, `5`, `−`, `+`, `RPE`, `7`,
   `8`, `9`, `10`, `SAVE SET`.
7. `07-rest`: `REST`, `2:41`, `NEXT`, `142.5 kg × 5`, `+30 SEC`, `SKIP`,
   `EDIT LAST`.
8. `08-plate-loading`: `142.5 KG`, `61.25 PER SIDE`, `20`, `20`, `10`,
   `10`, `1.25`, `READY`.
9. `09-exercise-switcher`: `3 / 14 SETS`, `BACK SQUAT`, `3 / 5`,
   `BENCH PRESS`, `0 / 5`, `DEADLIFT`, `0 / 4`, `JUMP`.
10. `10-missed-set`: `SET MISSED`, `140 kg × 3`, `TARGET 5 REPS @8`,
    `DROP TO 135`, `LOG 3`, `RETRY`, `135 NEXT`.
11. `11-summary`: `WORKOUT COMPLETE`, `14 SETS`, `6,420 KG`, `1:07:32`,
    `NEW 5RM`, `142.5 KG`, `DONE`.
12. `12-resume`: `WORKOUT PAUSED`, `BACK SQUAT · SET 4 OF 5`,
    `142.5 kg × 5`, `SAVED 24 SEC AGO`, `RESUME`, `DISCARD`.

## Approved workout-domain additions

- `SetTargetHero`: binds load, unit, target reps, set type, and set progress
  into one glanceable prescription.
- `PreviousPerformance`: presents the last comparable set beside the current
  prescription without opening analytics.
- `RpeSelector`: a bounded 7–10 exertion selector optimized for the active
  lifting flow.
- `RestNextSet`: keeps the rest deadline and next physical prescription
  visible in one state.
- `PlateLoadDiagram`: converts total bar weight into a mirrored per-side plate
  arrangement.
- `ActualVsTarget`: records a missed or partial set without turning it into a
  generic error dialog.
