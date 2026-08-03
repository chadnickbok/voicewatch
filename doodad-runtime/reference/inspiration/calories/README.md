# Calories inspiration

Captured on 2026-07-30 for Project Parallax design review. These files are
reference-only copies of official platform imagery; their copyright remains
with Apple or Google as applicable.

## Sources

| File | SHA-256 | Source |
| --- | --- | --- |
| `apple-activity-daily.png` | `f954c4085c2f5f26c688905252ffc8e6f3a205c7e2ac0d60c1a8a42827d31d69` | [Track daily activity with Apple Watch](https://support.apple.com/en-asia/guide/watch/apd3bf6d85a6/watchos) |
| `apple-activity-awards.png` | `1faeb51559abb341c2e703f9dbf232173d18a048a91ec0afbb121f8ada459895` | [Track daily activity with Apple Watch](https://support.apple.com/en-asia/guide/watch/apd3bf6d85a6/watchos) |
| `apple-change-goal.png` | `bb6ae440570897f74b97ee40f2bcf8de046788f167284b938cc9559b5827251e` | [Adjust Activity ring goals on Apple Watch](https://support.apple.com/guide/watch/adjust-your-activity-ring-goals-apd29b30023c/watchos) |
| `wear-glanceable.png` | `147e0a022de5cf44a485d37db9f30cf06a4341426843e854477bccb9302c799b` | [Design for Wear OS](https://developer.android.com/design/ui/wear/guides/get-started/design-for-wearables) |
| `wear-progress-usage.png` | `fc1796684190e0fa5995b877d9a089048606bd7ba824b69e6602cf6df3e8ac2a` | [Wear OS progress indicators](https://developer.android.com/design/ui/wear/guides/m2-5/components/progress-indicator) |

The Apple files were fetched from direct image URLs embedded in the linked
guides. The Google files are first-party Wear OS design illustrations.

## Observations used by the oracle

- Launch directly into today's energy total and remaining goal instead of an
  app title or provider shell.
- The total is the hero; goal status, progress, and the latest meal are its
  supporting scan path.
- Quick add should keep the amount control and decisive save action visible in
  one wrist-sized screen.
- A voice result needs a review state before it changes the daily total.
- Over-goal state should be explicit in text as well as a full progress track.
- The square adaptation uses the full 240 × 240 panel while preserving
  Material color, typography, shape, state, and 48 dp touch semantics.
