# Workout inspiration

Captured on 2026-07-30 for Project Parallax design review. These files are
reference-only copies of official platform imagery; their copyright remains
with Apple or Google as applicable.

## Sources

| File | SHA-256 | Source |
| --- | --- | --- |
| `apple-workout-selection.png` | `c5fa5fde25c59a3251243715e738794f748988db8b9fa22b1160c1dd407c3c76` | [Start a workout on Apple Watch](https://support.apple.com/en-euro/guide/watch/apd4edc9bc20/watchos) |
| `apple-running-metrics.png` | `4074bf69d5bee2656548a22a45ab0e1077847f579918f47372c8c9ebf2194133` | [Start a workout on Apple Watch](https://support.apple.com/en-euro/guide/watch/apd4edc9bc20/watchos) |
| `apple-custom-workout.png` | `a539fc2f3ffe4c8a09adea483bcdbbc42930417f9ae9bf42a81449c6affb7a50` | [Customize workouts on Apple Watch](https://support.apple.com/en-lamr/guide/watch/apd6b0679060/watchos) |
| `apple-workout-views.png` | `12419a17711f93234525b6d53c44ffd6e6a9c188bc22fd2f67243170da60d6fc` | [Customize workouts on Apple Watch](https://support.apple.com/en-lamr/guide/watch/apd6b0679060/watchos) |
| `android-exercise-ongoing-notification.png` | `8b779b14e0ee3daa5ddf0690b8e93a92d909cba50c7cae3557d6e57032121d91` | [Android Health Services Exercise sample](https://github.com/android/health-samples/tree/main/health-services/ExerciseSampleCompose) |

The Apple assets were fetched from the direct image URLs embedded in the
linked guides. The Android image is checked into Google's official Health
Services sample.

## Observations used by the oracle

- The launch state should show the active exercise and current set, not an app
  title or setup shell.
- The value being changed needs the strongest numeral treatment, with minus
  and plus controls remaining one-tap targets.
- Target reps, the next rest interval, and completion belong in one scan path.
- Rest is a distinct full-screen state with a dominant timer and an immediate
  next-set action.
- The next set carries the committed weight forward rather than silently
  returning to a default.
- Summary is compact: completed sets, total volume, exercise detail, and one
  clear repeat action.
- The square adaptation uses the full 240 × 240 panel while preserving
  Material color, typography, shape, state, and touch semantics.
