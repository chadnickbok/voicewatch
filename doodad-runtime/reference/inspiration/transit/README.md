# Transit reference material

These images are research inputs only. They are not packaged into the Doodad
app.

| File | First-party source | SHA-256 | Useful signal |
| --- | --- | --- | --- |
| `citymapper-watch-departures.jpeg` | [Citymapper on Apple Watch](https://citymapper.com/news/1645/citymapper-on-apple-watch) | `f7c7d8995d33cf0ec801e7c25bb2d6bc1aa1742cd00a458054290df25e060c16` | A wrist-first departures screen makes the next arrival dominant and keeps the following arrival secondary. |
| `citymapper-watch-journey.jpeg` | [Citymapper on Apple Watch](https://citymapper.com/news/1645/citymapper-on-apple-watch) | `bf028aef6dea7fc5bcf92d65e7d8b073baea61c85569987798cd1869e8469bc8` | Journey context is reduced to current leg, ETA, route, and a single next action. |
| `apple-watch-maps.png` | [Apple Watch Maps directions guide](https://support.apple.com/en-euro/guide/watch/apdea7480950/watchos) | `9b6b9cc5ef041b8b902c7eea856f92e9b177a42325835371c36dca3f86a0d146` | Official square-watch geometry uses a dominant trip summary over supporting route context rather than a persistent app heading. |

Google's current Wear OS guidance also confirms public-transport routes on
the watch, ETA visibility, route overview, and upcoming-step access:
[Use Google Maps on your Wear OS device](https://support.google.com/wearos/answer/6056852).

The Doodad oracle adopts the hierarchy—not the old Apple geometry or product
branding: station/line context, one dominant arrival, compact secondary
departures, explicit stale/disruption states, and immediately reachable
actions.
