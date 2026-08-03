# Weather concepts — exact-grid recompose v1

This folder contains the first deterministic recomposition of the five Weather
concepts onto Doodad's real display. It is a layout study, not runtime UI and
not yet an approved visual specification.

## Display contract

- Logical canvas: **192×192dp**
- Physical canvas: **240×240px**
- Scale: **1dp = 1.25px**
- Layout rhythm: **4dp = 5px**
- Normal touch target: **48dp = 60px**
- App title bar: none; Weather owns the whole display

Open `weather-layout-v1.html` through the Codex visualization viewer or a local
HTML preview. Its **4dp grid** and **Touch regions** controls expose the physical
grid and interactive bounds without changing the underlying layout.

## First-pass composition

| Screen | Exact-grid decision |
| --- | --- |
| Current | Large 68px temperature, condition and high/low hierarchy above a full-width 240×60 action region. The small freshness chip is visually quiet but sits inside a 60×60 target. |
| Hourly | A compact summary, simplified rain chart, four noninteractive forecast tiles, and a 240×60 Daily action region. Redundant y-axis labels were removed. |
| Daily | Four 43px informational rows plus page position. Rows are not controls; navigation is intended to come from the horizontal pager. |
| Details | Four 108×88 informational metric cards plus page position. Cards are not controls. |
| Rain | Hero warning, 13-bar minute chart with only four time labels, then a 166×60 Details target and 74×60 freshness target. |

The four hourly tiles remain for this pass because they are informational and
do not each require 60px touch bounds. The implementation should fall back to
three tiles if the real font metrics or large-font adaptation make four feel
cramped.

## Decisions still open

- Large-font behavior, especially at a 1.3 font scale
- Horizontal-pager interaction and its relationship to system back gestures
- Final four-versus-three hourly tile choice after real Roboto metrics land
- Whether the most expressive container shapes are affordable in LVGL without
  excessive clipping or mask cost
- Final color, typography, spacing, and shape token approval

## Files

- `weather-layout-v1.html` — deterministic, interactive exact-grid layout lab
- `weather-layout-v1.png` — rendered overview, generated from the HTML source

The interactive viewer can expose grid and touch overlays directly; dedicated
overlay PNGs can be generated later if they are useful as review artifacts.
