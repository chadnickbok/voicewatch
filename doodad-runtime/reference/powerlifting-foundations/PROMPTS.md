# Powerlifting concept-board prompts

All four assets use the built-in image-generation path and the `ui-mockup`
taxonomy. These prompts intentionally repeat the visual system so each board
can stand alone while remaining part of one product family.

## Shared direction

```text
Use case: ui-mockup
Asset type: shippable smartwatch app flow board
Primary request: three coherent screens from one powerlifting workout tracker
Device: square 240 x 240 pixel watch display, shown flat and front-on without a
watch body, bezel, hands, reflections, perspective, or environmental scene
Style/medium: realistic production UI, Material 3 Expressive adapted
thoughtfully to a square watch, not concept art
Typography: precise Roboto-like sans serif with tabular numerals
Color palette: near-black navy #030914 background; deep indigo #102342 and
#19345d surfaces; lavender #d6afff primary actions; off-white #f4f5ff primary
text; blue-gray secondary text; mint #72e3ad success; coral #ff8c8c failure;
amber #ffc857 warmup and plate assistance
Composition/framing: a clean horizontal three-panel flow board; each panel is
an equally sized perfect square watch framebuffer with small flow arrows and a
short panel label outside the framebuffer; generous separation; every screen
uses all available space
Component language: large hero number, expressive rounded or softly
asymmetric surfaces, compact status pills, grouped controls, 48dp touch
targets, one dominant action, restrained depth and glow
Constraints: no generic app title bar; no phone UI; no circular-display
cropping; no Apple rings; no logos; no trademarks; no watermark; no tiny
spreadsheet; no decorative gym photography; all text horizontal and legible;
render supplied text verbatim and do not invent extra text
```

## 01 — Start and plan

```text
Create three sequential screens labeled outside the frames "TODAY",
"SESSION", and "EXERCISE".

Screen 1 exact UI copy: "HEAVY DAY", "SAT · WEEK 4", "SQUAT · BENCH ·
DEADLIFT", "14 SETS", "READY", "START WORKOUT". Make HEAVY DAY the hero
inside an expressive lavender-indigo surface; show three compact exercise
markers and a full-width primary start action.

Screen 2 exact UI copy: "HEAVY DAY", "0 / 14 SETS", "BACK SQUAT", "5 SETS",
"BENCH PRESS", "5 SETS", "DEADLIFT", "4 SETS", "BEGIN SQUAT". Use three
large glanceable rows; Back Squat is selected and more prominent.

Screen 3 exact UI copy: "CHOOSE EXERCISE", "RECENT", "BACK SQUAT",
"FRONT SQUAT", "PAUSED SQUAT", "+ CUSTOM". Use a compact filter pill and
three expressive list rows; Back Squat is selected with a mint check.
```

## 02 — Perform and log

```text
Create three sequential screens labeled outside the frames "ACTIVE SET",
"WEIGHT", and "RESULT".

Screen 1 exact UI copy: "BACK SQUAT", "SET 3 OF 5", "140", "kg", "× 5",
"LAST 137.5 × 5 @8", "COMPLETE SET". The 140 kg prescription is the dominant
hero. Set progress dots sit near the exercise name. Previous performance stays
visible above a full-width lavender primary action.

Screen 2 exact UI copy: "WEIGHT", "140.0", "kg", "−5", "−2.5", "+2.5",
"+5", "20 · 20 · 10 · 2.5", "DONE". Make the value enormous; use a balanced
four-button adjustment group, a small amber plate-load preview, and one clear
done action.

Screen 3 exact UI copy: "SET 3", "140 kg", "REPS", "5", "−", "+", "RPE",
"7", "8", "9", "10", "SAVE SET". Use a large rep stepper and compact RPE
choice group with 8 selected. Keep SAVE SET dominant and reachable.
```

## 03 — Rest and adapt

```text
Create three sequential screens labeled outside the frames "REST",
"PLATES", and "SWITCH".

Screen 1 exact UI copy: "REST", "2:41", "NEXT", "142.5 kg × 5", "+30 SEC",
"SKIP", "EDIT LAST". The timer is the hero, but the next prescription must
remain clearly visible. Put +30 SEC and SKIP in a two-button group and keep
EDIT LAST as a quiet tertiary action.

Screen 2 exact UI copy: "142.5 KG", "61.25 PER SIDE", "20", "20", "10",
"10", "1.25", "READY". Show a clean horizontal Olympic barbell diagram with
color-coded plate silhouettes mirrored around the bar; keep labels readable
and use amber only as assistance, not as the main action.

Screen 3 exact UI copy: "3 / 14 SETS", "BACK SQUAT", "3 / 5", "BENCH PRESS",
"0 / 5", "DEADLIFT", "0 / 4", "JUMP". Use three large exercise rows with
progress indicators; Back Squat is active, Bench Press is selected as the jump
target, and JUMP is the lavender primary action.
```

## 04 — Outcomes and recovery

```text
Create three sequential screens labeled outside the frames "MISSED SET",
"COMPLETE", and "RESUME".

Screen 1 exact UI copy: "SET MISSED", "140 kg × 3", "TARGET 5 REPS @8",
"DROP TO 135", "LOG 3", "RETRY", "135 NEXT". Use a restrained coral status
surface, not an alarming error page. Show actual versus target and make the
recommended next-load action prominent.

Screen 2 exact UI copy: "WORKOUT COMPLETE", "14 SETS", "6,420 KG",
"1:07:32", "NEW 5RM", "142.5 KG", "DONE". Use a restrained mint completion
symbol, a three-metric grid, and a small PR card. No confetti; DONE is the only
primary action.

Screen 3 exact UI copy: "WORKOUT PAUSED", "BACK SQUAT · SET 4 OF 5",
"142.5 kg × 5", "SAVED 24 SEC AGO", "RESUME", "DISCARD". Make the durable
saved state obvious. RESUME is a large lavender action; DISCARD is small,
coral, and visually separated to prevent accidental loss.
```
