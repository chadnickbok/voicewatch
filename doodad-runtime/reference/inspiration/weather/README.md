# Weather design references

Retrieved 2026-07-30 for internal visual research. These third-party images
remain the property of their respective publishers and are not product assets.

The [`generated-mockups/`](generated-mockups/) directory contains the original
square-device concepts derived from this research, including 240×240 review
copies and a contact sheet.

## Sources

| Local file | Source page | Direct image | SHA-256 |
|---|---|---|---|
| `pixel-weather-current.jpg` | [Android Central: Pixel Weather is making its way to Pixel Watch](https://www.androidcentral.com/apps-software/pixel-weather-app-coming-to-pixel-watch) | [current conditions and tiles](https://cdn.mos.cms.futurecdn.net/DxbkxK7ruQxz3zgZJ4xwHm.jpg) | `9fa61e757dfa31dc9bf7bf0c4fa121828592bf25a8355b9a9d8b8aa77aeffd45` |
| `pixel-weather-details.jpg` | [Android Central: Pixel Weather is making its way to Pixel Watch](https://www.androidcentral.com/apps-software/pixel-weather-app-coming-to-pixel-watch) | [sunrise and UV cards](https://cdn.mos.cms.futurecdn.net/h4FqEynfwhevcTesSig4Dm.jpg) | `f480b3af68aecad33f673d3d558c32226e7bc5a55bbd9fae286bd135ee518200` |
| `apple-watch-weather-overview.png` | [Apple Support: Check the weather on Apple Watch](https://support.apple.com/guide/watch/check-the-weather-apd07ec24f9e/watchos) | [weather overview](https://help.apple.com/assets/692F45642C5AEF37BA06084A/692F4568B3C902EE690E2EBC/en_US/9dbe99020ea285a1393147c7e290a8e9.png) | `26522d95edc3904e0804f05e4cfc0ce43c047f704be6fcb18702c04f6e701ffb` |
| `wear-os-weather-surfaces.png` | [Android Developers: Principles of Wear OS development](https://developer.android.com/training/wearables/principles) | [full weather app surface](https://developer.android.com/static/wear/images/principles_updated_5.png) | `c72abdea8faa74f61bd200396d096bea7b2bb4bb4d8c6fe3a6875bae9641bae8` |

The Pixel Weather screenshots are credited by Android Central to 9to5Google.
The Apple and Android developer images are first-party platform guidance.

## User-supplied references

Saved 2026-08-01 from the screenshots supplied for the Doodad Weather review.
The original publishers were not included with the attachments, so these are
kept as internal visual references rather than redistributable product assets.

| Local file | Reference | SHA-256 |
|---|---|---|
| `apple-watch-weather-metric-gauges.png` | Apple Watch metric/gauge views for temperature, air quality, and humidity | `d1318b08a1d988c408863cba521e8d1ba1ea387a1ab92c09e2916d23ddba324a` |
| `apple-watch-weather-forecast-list.png` | Apple Watch daily forecast list | `5815313089bd95d02e6f39d1d462cbc07fd82e12e617ef511ad2ca51e61a948e` |
| `round-watch-weather-multiday.png` | Round-watch dense multi-day forecast | `e343dbe0648e24416112a2aa47f2352dd6449d8cbe23d7ed7c5f183738d18dfe` |
| `round-watch-weather-current-conditions.png` | Round-watch current-conditions card | `ec8a0bfd929a146fe3001820ab758595875a77877dd289146c0dd3d78f0055c8` |
| `pebble-weather-current-conditions.png` | Pebble-style glanceable current conditions with freshness status | `fa2fc65622eea03e8544af1931187126e93390e33f3716d040059cadb5e891a0` |

## Observed design language

- Location is compact context, not a generic app title bar.
- Current temperature is the hero and is paired with one immediately
  recognizable condition mark.
- High, low, precipitation, and freshness are supporting information rather
  than equal-weight stacked rows.
- Current conditions live on one large rounded/tonal surface; detailed
  forecasts become later scroll positions or separate cards.
- A weather surface should answer “now” in a glance and keep refresh or
  recovery actions reachable without displacing the forecast.
- None of the supplied references gives refresh a large, high-emphasis control.
  Freshness is either passive or communicated in text such as “UPDATED NOW.”
- The square Apple composition confirms that the same hierarchy works without
  round-screen edge transforms.

These observations guide a Doodad interpretation rather than pixel copying.
