# Timer design references

Retrieved 2026-07-30 for internal visual research. These third-party images
remain the property of their respective publishers and are not product assets.

## Sources

| Local file | Source page | Direct image | SHA-256 |
|---|---|---|---|
| `apple-watch-running-timer.png` | [iPhone Life: How to Set a Timer on Apple Watch](https://www.iphonelife.com/content/how-to-set-timer-apple-watch) | [running timer image](https://www.iphonelife.com/sites/iphonelife.com/files/styles/2023_applewatchultra_570_2x/public/img_8905_1.png) | `1092764d33a6f3e92c4a37516f2a21cfd904f4a959a4c6c40121564108056d3b` |
| `pixel-watch-4-timer-tile.jpg` | [9to5Google: Pixel Watch 4 Wear OS 6 tiles](https://9to5google.com/2025/08/21/all-new-wear-os-6-tiles-on-pixel-watch-4-gallery/) | [timer image](https://9to5google.com/wp-content/uploads/sites/4/2025/08/Pixel-Watch-4-tiles-timer.jpg?quality=82&strip=all&w=1600) | `2b660875740249e887cabef4c2200fb370b407d8fa09d0f19e768d466c977fa3` |
| `wear-os-clock-timer.png` | [9to5Google: Google Clock Wear OS M3 Expressive icons](https://9to5google.com/2025/09/22/google-clock-wear-os-m3-expressive-icons/) | [timer image](https://i0.wp.com/9to5google.com/wp-content/uploads/sites/4/2025/09/Google-Clock-Wear-OS-icons-old-3.png?resize=456%2C456&ssl=1) | `a819cc862ffe50956b33cb3de2a5c798087026459b810c29a8a4d1ba6040f2a5` |

## Observed design language

- The countdown is the visual hero, centered inside or immediately adjacent
  to a progress dial.
- The primary running-state action is large, isolated, and reachable at the
  lower edge; secondary reset/cancel actions are smaller.
- Status is communicated through the dial and action glyphs instead of a
  sentence-length caption.
- High-contrast accent color is reserved for progress and the primary action.
- The Apple square composition proves that the circular countdown metaphor
  remains effective without cropping the whole interface to a round viewport.

These observations are input to a Doodad interpretation, not instructions to
copy a publisher's pixels or platform-specific chrome.
