# Calendar inspiration

Captured on 2026-07-30 for Project Parallax design review. These files are
reference-only copies of official product and design-guidance imagery; their
copyright remains with Apple or Google as applicable.

## Sources

| File | SHA-256 | Source |
| --- | --- | --- |
| `apple-calendar-day-list.png` | `e7acaae64d49d6e0801e6b8f0748436f9a4c4bb78070236f676d86fb1e7ed898` | [View and add events in Calendar on Apple Watch](https://support.apple.com/guide/watch/calendar-apd1b51754cc/watchos) |
| `apple-calendar-event-details.png` | `40a47eabad9193368bdf75da4dd643772a93f6670863ecd0727195848c0a449d` | [View and add events in Calendar on Apple Watch](https://support.apple.com/guide/watch/calendar-apd1b51754cc/watchos) |
| `apple-calendar-new-event.png` | `f6f5c1fd388b1f647206fc2b724ba3619a46100fc82b393a54b921857f919e1b` | [View and add events in Calendar on Apple Watch](https://support.apple.com/guide/watch/calendar-apd1b51754cc/watchos) |
| `wear-principles-relevant-content.png` | `ac4c5d8bf8474fe22b4fa78f8556635af39a5a48205a5e40b56d1bf4fce426f3` | [Principles of Wear OS development](https://developer.android.com/training/wearables/principles) |

The Apple assets were fetched from the direct image URLs embedded in the
linked guide. The Google image is the official “relevant content” principle
illustration.

## Observations used by the oracle

- The useful launch state is the agenda itself, not a title or intermediate
  “open calendar” page.
- Time, event name, and place are the dominant scan path.
- A compact event card can still expose full-row tap behavior and a 48 dp
  target.
- Event detail should preserve time and location while putting the immediate
  RSVP decision within one tap.
- Time-zone and offline state are contextual content, not generic provider
  diagnostics.
- The square adaptation keeps Material color, typography, shape, state, and
  touch semantics while using the full 240 × 240 panel.
