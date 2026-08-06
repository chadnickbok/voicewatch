# App visual direction

Status: **approved build reference**

This directory is the canonical visual starting point for Doodad watch UI work. It contains screens that were selected, implemented, and exercised on the watch. It is intentionally not a mood board.

Use `reference/inspiration/` for broad product research and rejected or exploratory directions. Use this directory when generating concepts that should look like Doodad.

## Approved images

| File | What it anchors | Source |
|---|---|---|
| `01-home.png` | Shell hierarchy, oversized time, compact status row, Apps and Voice actions | `evidence/watch-shell/system-shell-integrated/01-watch-face.png`, production shell commit `c07ffa9` |
| `02-app-launcher.png` | App-list typography, outlined rows, icon tiles, accent ownership, scroll density | `evidence/watch-shell/app-launcher-v6/01-launcher.png`, production launcher commit `c73c16a` |
| `03-voice-listening-quiet.png` | Voice overlay composition and resting animation state | `evidence/watch-shell/voice-overlay-animation-v6/00-quiet.png`, production voice commit `c85dc89` |
| `04-voice-listening-active.png` | Voice overlay motion language and active state | `evidence/watch-shell/voice-overlay-animation-v6/03-pulse.png`, production voice commit `c85dc89` |
| `05-app-surface-weather.png` | A real full-screen app surface on the target hardware | `evidence/hardware/system-shell-c73c16a/final/ordered/03-weather.png` |
| `06-on-device-flow.png` | End-to-end hardware appearance across Home, Apps, an app, and Voice | `evidence/hardware/system-shell-c73c16a/final/on-device-shell.png` |

## Visual grammar to preserve

- True black canvas with high-contrast content; avoid generic dashboard cards, glass effects, and decorative gradients.
- Large, condensed, emphatic display type paired with compact supporting text.
- Electric accents on black: blue/violet for navigation and listening, coral/red for Voice and cancel, acid green for live status, and app-owned colors elsewhere.
- Bold rounded pills and outlined rows with simple semantic icons.
- One dominant idea per 240 × 240 screen, with large touch targets and very little ornamental chrome.
- The shell should remain immediately recognizable. Standalone apps can establish their own accent color while retaining this typography, contrast, geometry, and interaction density.

For the Agent Home work specifically, the watch face should gain only a small, glanceable indication that agents are active. The detailed queue, status list, and drill-down belong inside the standalone Agent Home app and should use the launcher/app grammar shown here—not a desktop-style dashboard shrunk onto the watch.

## Curation rule

Only add an image here after it has been explicitly chosen as a build target or has shipped as the accepted implementation. When direction changes, replace superseded references instead of accumulating alternatives. Every image should retain its source path and relevant commit in the table above.
