# Powerlifting foundations concept

This directory defines the visual and structural target for the Doodad
powerlifting app before it is translated into shared AppSpec components,
Wear Compose, and LVGL. The generated concept boards are design inputs, not
shipping framebuffer goldens.

## Product boundary

The watch is the workout execution surface. Routine construction, long-term
analytics, and program authoring may live on a phone or be voice-generated,
but the complete active workout must remain usable and recoverable without a
phone connection.

The first reference scenario is a heavy squat, bench, and deadlift session.
It exercises exercise selection, planned and actual values, set types, RPE,
plate loading, rest, substitutions, failure, PRs, completion, and crash
recovery.

## Visual contract

- 240 x 240 physical pixels and a 192 x 192dp logical viewport.
- Full-bleed launched-app content; no generic app title bar.
- Roboto with tabular numerals for load, reps, RPE, and timers.
- Near-black navy background with deep indigo surfaces.
- Lavender is the primary action and current-prescription role.
- Mint is reserved for completed/success/PR state.
- Coral is reserved for missed-set and destructive state.
- Warm amber identifies warmup sets and plate-loading assistance.
- Material 3 Expressive shape and state language adapted to a square screen.
- Minimum 48dp semantic touch target even when the visible control is compact.
- One dominant number or action per screen.
- Information required for the next physical action remains visible during
  rest and error handling.

## Decisive flow

```text
Today
  -> Session overview
  -> Exercise picker
  -> Active set
       -> Edit weight
       -> Record result / RPE
       -> Rest + next set
            -> Plate loading
            -> Switch exercise
            -> Next active set
       -> Missed set recovery
  -> Summary / PR

Any active state
  -> persisted interruption
  -> exact resume
```

## Concept-board screen inventory

| Board | Screen | Purpose | Dominant component |
| --- | --- | --- | --- |
| `01-start-and-plan` | Today | Start the prescribed session without setup friction | `SessionHero` |
| | Session overview | See lift order and progress | `ExerciseQueue` |
| | Exercise picker | Add or substitute an exercise | `ExercisePickerList` |
| `02-perform-and-log` | Active set | Execute the current prescription | `SetTargetHero` |
| | Weight editor | Make a fast load correction | `NumericEditor` |
| | Set result | Confirm actual reps and optional RPE | `SetResultEditor` |
| `03-rest-and-adapt` | Rest | Recover while preparing the next set | `RestTimerHero` |
| | Plate loading | Translate total load into plates per side | `PlateLoadDiagram` |
| | Exercise switcher | Reorder around occupied equipment | `ExerciseQueue` |
| `04-outcomes-and-recovery` | Missed set | Record reality and choose a safe next action | `ActualVsTarget` |
| | Session summary | Review the useful result, including a restrained PR moment | `WorkoutSummary` |
| | Resume | Restore the exact durable active-workout state | `ResumeCard` |

## Component hierarchy

```text
PowerliftingApp
|- FullScreenAppScaffold
|  |- SessionStatus
|  |- RoutedContent
|  `- ContextualBottomAction
|- TodayScreen
|  |- SessionHero
|  |- ExercisePreviewStrip
|  `- PrimaryAction
|- SessionOverviewScreen
|  |- SessionProgress
|  `- ExerciseQueue
|     `- ExerciseRow
|- ExercisePickerScreen
|  |- FilterChipGroup
|  `- ExercisePickerList
|     `- ExerciseRow
|- ActiveSetScreen
|  |- ExerciseContext
|  |- SetProgressIndicator
|  |- SetTargetHero
|  |- PreviousPerformance
|  `- CompleteSetAction
|- NumericEditorScreen
|  |- ValueHero
|  |- IncrementButtonGroup
|  |- PresetChipGroup
|  `- PlateLoadPreview
|- SetResultScreen
|  |- ActualSetSummary
|  |- RepStepper
|  |- RpeScale
|  `- SaveSetAction
|- RestScreen
|  |- RestTimerHero
|  |- NextSetPreview
|  |- TimerAdjustmentGroup
|  `- CorrectLastSetAction
|- PlateLoadingScreen
|  |- TotalLoadHeader
|  |- PlateLoadDiagram
|  `- PlateLegend
|- ExerciseSwitcherScreen
|  |- ExerciseQueue
|  `- FinishWorkoutAction
|- MissedSetScreen
|  |- ActualVsTarget
|  |- RecoverySuggestion
|  `- RecoveryActionGroup
|- SummaryScreen
|  |- CompletionHero
|  |- SummaryMetricGrid
|  |- PersonalRecordCard
|  `- DoneAction
`- ResumeScreen
   |- ResumeCard
   |- LastDurableState
   `- ResumeOrDiscardActions
```

## Shared framework primitives required

These should remain semantic AppSpec components rather than workout-specific
renderer branches:

- `FullScreenAppScaffold`
- `ExpressiveSurface`
- `HeroValue`
- `StatusPill`
- `ProgressDots`
- `LinearProgress`
- `ButtonGroup`
- `CompactButton`
- `FilledAction`
- `TonalAction`
- `DangerAction`
- `ListRow`
- `MetricTile`
- `NumericEditor`
- `TimerHero`
- `Confirmation`
- `TransientCelebration`

Workout-specific components should compose those primitives and carry the
domain semantics: `SetTargetHero`, `PreviousPerformance`, `SetResultEditor`,
`PlateLoadDiagram`, `ActualVsTarget`, and `WorkoutSummary`.

## State and event contract

The Wasm guest should own the workout state machine. Renderers receive a
bounded semantic scene and emit commands; neither renderer infers program
logic.

Minimum commands:

- `startWorkout`
- `selectExercise`
- `substituteExercise`
- `adjustWeight`
- `adjustReps`
- `setRpe`
- `setSetType`
- `completeSet`
- `correctLastSet`
- `extendRest`
- `skipRest`
- `jumpExercise`
- `recordMissedSet`
- `finishWorkout`
- `resumeWorkout`
- `discardWorkout`

Every accepted command must persist the active session before visual
acknowledgement. Rest uses an absolute deadline so it can recover after app
termination. The product framebuffer must never depend on a live phone sync.

## Concept use

The concept boards deliberately show more screens than the current Workout
demo. Implementation should first extract the shared primitives, then build
the active-set/rest spine, then add planning, adaptation, and recovery. Each
screen becomes a deterministic scenario rendered by both Wear Compose and
LVGL before being promoted to a conformance golden.

## Concept iterations

- `generated/concepts/` is the rejected first exploration. It established the
  flow and hierarchy, but its portrait frames, condensed display type, glossy
  borders, and bespoke exercise medallions drift from the Weather visual
  system.
- `generated/concepts-v2/` is the implementation-facing direction. Each
  screen is generated individually as a square framebuffer from the approved
  Weather visual master, then flow boards are assembled without restyling.
  V2 uses the shared Wear Compose and LVGL primitives for generic interaction,
  while permitting new workout-domain components when the physical task has
  no honest existing equivalent.

## New-component test

A workout-specific component is justified when all of the following are true:

1. No existing shared component communicates the domain state or physical
   action without distortion.
2. The new component still uses the shared typography, color roles, spacing,
   shape vocabulary, motion character, and interaction semantics.
3. Its state and events can be represented in AppSpec without renderer-local
   program logic.
4. It has a bounded implementation in both Wear Compose and LVGL.
5. It remains useful across multiple strength-training scenes rather than
   existing only as decoration for one screenshot.

By that test, `SetTargetHero`, `PreviousPerformance`, `PlateLoadDiagram`,
`RpeSelector`, `RestNextSet`, and `ActualVsTarget` are warranted. Bespoke
exercise medallions, a second button language, decorative card borders, and a
workout-only display typeface are not.

## V2 generated review surfaces

- `generated/concepts-v2/screens/` contains the twelve individual square
  framebuffer concepts.
- `generated/concepts-v2/flows/01-start-and-plan.png`
- `generated/concepts-v2/flows/02-perform-and-log.png`
- `generated/concepts-v2/flows/03-rest-and-adapt.png`
- `generated/concepts-v2/flows/04-outcomes-and-recovery.png`

## Implementation status

The V2 direction is now implemented as a dual-renderer app rather than a
concept-only board.

- All twelve screens are deterministic AppSpecs generated from
  `tools/powerlifting_foundations/generate_appspecs.py`.
- `apps/workout/src/lib.rs` executes the complete decisive flow in Wasm.
- Wear Compose and LVGL implement the same bounded Powerlifting composition on
  the 192dp/240px square profile.
- The checked-in trace has 19 accepted operations and 20 replayable
  checkpoints.
- Project Parallax compares all twelve states with exact structure and bounds
  and zero quality findings in either renderer.
- The active set has also been rendered on the real API 37 square Wear
  emulator at 240x240 and 200dpi.

Evidence and the detailed result live in `evidence/` and
`../reviews/workout.md`.
- `generated/concepts-v2/flows/all-screens.png` is the compact review sheet.

The individual screens were generated with the built-in image tool from the
checked-in Weather visual master. The flow boards are mechanical montages;
they do not introduce a second visual interpretation.
