# Square Weather mockups

Generated 2026-08-01 as high-fidelity visual exploration for a 240×240
Doodad display. These are design references, not runtime assets.

## Screens

| Screen | Full-resolution concept | 240×240 review copy |
|---|---|---|
| Current conditions | `current-conditions.png` | `current-conditions-240.png` |
| Hourly forecast | `hourly-forecast.png` | `hourly-forecast-240.png` |
| Daily forecast | `daily-forecast.png` | `daily-forecast-240.png` |
| Weather details | `weather-details.png` | `weather-details-240.png` |
| Rain approaching | `rain-in-20-minutes.png` | `rain-in-20-minutes-240.png` |

`weather-series-240-contact-sheet.png` shows the five review copies together.

## Shared generation prompt

> Create one shippable, high-fidelity square smartwatch weather screen inspired
> by Material 3 Expressive and thoughtfully adapted to a 240×240 square
> display. Use the supplied weather screenshots only as references for
> information hierarchy and glanceability. Render a flat, full-bleed square UI
> screenshot with no watch body, bezel, perspective, title bar, circular crop,
> logo, or watermark. Use a near-black navy background, cool periwinkle and
> sky-blue tonal surfaces, warm yellow only for sun, expressive squircle and
> pill shapes, confident scale contrast, crisp sans-serif typography, simple
> vector weather glyphs, and clear touch targets. Use the whole display. Avoid
> dense labels and a prominent refresh button; communicate freshness only with
> quiet status text.

## Screen-specific prompts

- **Current conditions:** San Francisco; 62°; partly cloudy; H 67°; L 54°;
  feels like 59°; Updated now; bottom Hourly action.
- **Hourly forecast:** Now 62° and partly cloudy; precipitation timeline;
  NOW/10/11/12 forecast cards at 62°/63°/65°/66°; Rain 0%; bottom Daily action.
- **Daily forecast:** San Francisco; TODAY/MON/TUE/WED rows with weather glyphs
  and 54°–67°/53°–65°/51°–63°/52°–64°; selected tonal TODAY row; Updated now.
- **Weather details:** 62° and partly cloudy; expressive 2×2 metric grid for
  HUMIDITY 49%, WIND 8 mph, UV 3 Low, and SUNRISE 6:12; Updated now.
- **Rain approaching:** Rain in 20 min; Light rain for 35 min; 32%; a 60-minute
  precipitation graph; bottom Details action; Updated now.

Generated with the built-in image-generation tool. Full-resolution outputs are
1254×1254; the review copies are downsampled to the physical display resolution
to expose readability and density problems early.
