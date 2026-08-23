# Workout app flows

This document is the implementation contract for planning, performing, and
recovering a strength workout on the 240×240 Doodad watch. It covers the
complete user journey represented by the sixteen deterministic screens in the
Workout app.

## Product model

The watch is optimized for decisions made at the rack: what to lift next,
what load to use, what actually happened, and how to adapt. It also supports a
bounded setup flow so a user can adjust the current routine and measurable
strength goal without reaching for a phone. Free-form naming, long program
cycles, and deep analytics remain better suited to phone or voice surfaces.

The durable domain objects are:

- `TrainingPlan`: name, ordered exercises, work sets, target reps, starting
  load, rest duration, and automatic warmup policy.
- `StrengthGoal`: lift, measurement (`5RM`), target load, baseline load, and
  timeframe.
- `WorkoutSession`: plan snapshot, current exercise/set, actual results, RPE,
  absolute rest deadline, status, and summary metrics.
- `SetResult`: prescribed load/reps, actual reps, RPE, outcome, and recovery
  choice.

Plan and goal edits affect future sessions. Starting a workout snapshots the
current plan, so later edits never rewrite an in-progress or completed
session.

## Screen and state map

| State | Screen id | User question | Primary action |
| --- | --- | --- | --- |
| Ready | `powerlifting.today` | What is scheduled today? | Start workout |
| Training hub | `powerlifting.training-hub` | Is my plan and goal right? | Open plan or goal |
| Workout builder | `powerlifting.workout-builder` | Which lifts are in Heavy Day? | Edit prescription or save |
| Exercise prescription | `powerlifting.exercise-prescription` | How many sets and reps? | Save exercise settings |
| Goal setup | `powerlifting.strength-goal` | What measurable result am I chasing? | Save goal |
| Session review | `powerlifting.session` | What is the order and total volume? | Begin first lift |
| Exercise picker | `powerlifting.exercise-picker` | Which lift should fill this slot? | Select lift |
| Active set | `powerlifting.active-set` | What do I perform now? | Complete set |
| Weight editor | `powerlifting.weight-editor` | Is the load correct? | Commit load |
| Set result | `powerlifting.set-result` | What actually happened? | Save reps and RPE |
| Missed set | `powerlifting.missed-set` | How should I recover safely? | Choose next prescription |
| Rest | `powerlifting.rest` | How long until the next set? | Prepare or skip |
| Plate loading | `powerlifting.plate-loading` | What plates go on each side? | Mark ready |
| Exercise switcher | `powerlifting.exercise-switcher` | Should I change lifts or finish? | Jump or finish |
| Summary | `powerlifting.summary` | What useful result did I produce? | Finish review |
| Resume | `powerlifting.resume` | Where was I interrupted? | Resume exact state |

## Journey 1: review or edit a workout

```text
Today
  -> tap the 14 SETS plan card
Training hub
  -> tap HEAVY DAY
Workout builder
  -> tap BACK SQUAT
Exercise prescription
  -> set work sets
  -> set target reps
  -> DONE
Workout builder
  -> optionally ADD EXERCISE -> Exercise picker -> Workout builder
  -> SAVE PLAN
Training hub
  -> DONE
Today
```

Behavioral details:

1. The hub shows one current plan and one measurable goal; it is not a dense
   settings dashboard.
2. Every interactive planning surface is at least 48dp. Compact rows are
   read-only summaries.
3. Adjusting sets or reps persists `workout.adjust_plan` before the watch
   acknowledges the new value.
4. `SAVE PLAN` persists `workout.save_plan`, clears transient planning
   context, and returns to the hub.
5. Adding an exercise reuses the existing exercise picker. The runtime keeps
   a bounded `planning` context flag so selection returns to the builder
   instead of the live-session review.
6. Back or app termination before `SAVE PLAN` leaves the last durable plan in
   place. A production provider may stage edits in a draft object, but it must
   not partially mutate the active workout.

## Journey 2: set a strength goal

```text
Today -> Training hub -> goal card -> Strength goal
  -> adjust 5RM target in 5 kg increments
  -> review current 5RM and 12-week timeframe
  -> SAVE GOAL
Training hub -> DONE -> Today
```

The goal is deliberately measurable: lift + rep maximum + target load +
timeframe. The UI does not use vague goals such as “get stronger.” The target
range is validated before persistence, and the current screen updates only
after `workout.adjust_goal` succeeds. `SAVE GOAL` commits the goal as a unit.

## Journey 3: start and perform the planned workout

```text
Today -> START WORKOUT -> Session review
  -> optional exercise selection/substitution
  -> BEGIN SQUAT
Active set -> optional load edit -> COMPLETE SET
Set result -> actual reps + RPE -> SAVE SET
  -> success: Rest
  -> partial/missed: Missed set recovery -> Rest or retry
Rest -> plate loading or next Active set
Active set -> switch exercise -> next lift
Exercise switcher -> finish -> Summary -> Today
```

Starting a session snapshots the current plan and persists
`workout.start` before mounting the active-set screen. The active set keeps
exercise, set number, load, target reps, and previous comparable performance
visible. The load card is the edit affordance; the primary button records the
set rather than silently assuming success.

## Journey 4: record a completed or partial set

After `COMPLETE SET`, the result screen defaults actual reps to the target and
offers RPE 7–10. The user adjusts only facts that differ from the prescription.

- Reps are bounded from 0–20 and persisted through `workout.adjust_reps`.
- RPE is optional domain detail but explicit when selected.
- `SAVE SET` calls `workout.complete_set` before routing.
- Actual reps at or above target route to rest.
- Actual reps below target route to the missed-set screen; the app never hides
  a failure by recording the target value.

The result can be corrected during rest by holding the next-set card. The
correction reopens the same result editor and must replace the durable result
instead of appending a duplicate set.

## Journey 5: log and recover from a failed set

The missed-set state presents actual and target together and offers three
different intents:

- `DROP TO 135`: accept the actual result and reduce the next prescription.
- `LOG 3`: preserve the actual result without an automatic load change.
- `RETRY`: return to the active-set prescription without advancing.

`135 NEXT` accepts the suggested reduction and enters rest. Coral is limited
to the missed-set status and destructive actions; it does not turn the whole
screen into an alarm. A failed provider write leaves the user on the current
screen with the previous durable result intact.

## Journey 6: rest, load plates, and adapt order

Rest stores an absolute deadline, not a decrementing local counter. Returning
from sleep or process death recomputes the displayed time from that deadline.
The rest screen always retains the next load and rep target.

- `+30` extends and persists the deadline.
- `SKIP` advances immediately to the next set.
- Tapping the next-set card opens the per-side plate diagram.
- Holding the next-set card edits the last result.
- Holding `COMPLETE SET` opens the exercise queue so the user can work around
  occupied equipment.
- Finishing is an intentional long-press from the queue, reducing accidental
  early completion.

## Journey 7: interruption and exact resume

Every accepted state-changing command persists before visual acknowledgment.
If the app is relaunched with an active session, the resume screen shows lift,
set number, load, reps, and save age. `RESUME` restores the exact active set;
`DISCARD` is visually destructive and returns to Today only after persistence.

The minimum durable resume payload is:

```text
session id
plan snapshot revision
exercise index + set index
prescribed and actual values
last committed set result
absolute rest deadline, if resting
pending recovery choice, if on missed-set screen
updated_at timestamp
```

## Command and persistence contract

| Command family | Validation | Durable effect |
| --- | --- | --- |
| Plan sets/reps | sets 1–10; reps 1–20 | Update draft plan |
| Save plan | Complete valid exercise list | Commit plan revision |
| Goal target | 50–300kg in 5kg steps | Update draft goal |
| Save goal | Lift, metric, target, timeframe present | Commit goal revision |
| Start | No other active session | Create plan snapshot and session |
| Adjust load | 20–400kg in 5kg steps | Update current prescription |
| Complete set | Current set unresolved | Append one idempotent result |
| Recovery | Partial result exists | Update next prescription/status |
| Rest change | Active rest deadline exists | Replace deadline |
| Finish | Active session exists | Finalize summary and clear active id |

Provider operations require an idempotency key in production. Retries may
repeat a request but may never duplicate a set, goal, or session.

## Accessibility and ergonomics

- All actionable surfaces are at least 48dp; small progress bars and summary
  rows are not interactive.
- Numerical values use tabular forms and include spoken units in semantics.
- Color reinforces state but never carries it alone (`SET MISSED`, `CURRENT`,
  and `WORKOUT COMPLETE` remain textual).
- Haptics should distinguish value changes, set commit, missed set, rest end,
  and workout completion.
- The primary physical action stays at the bottom edge where it is easiest to
  acquire with the opposite hand.

## Acceptance journeys

1. Edit Back Squat to 6 work sets, save the plan, set a 155kg 5RM goal, and
   return to Today without losing either accepted provider operation.
2. Start Heavy Day, change the load, complete only 3 of 5 reps, record RPE 8,
   choose 135 next, rest, inspect plates, switch exercise, and finish.
3. Terminate at active set, rest, and missed-set recovery; each relaunch must
   restore the last durable state with no duplicate set result.
4. Reject out-of-range plan, goal, load, and rep values without changing the
   framebuffer or durable provider state.
5. Render all sixteen screens through both Compose and LVGL with identical
   semantic hierarchy and bounds, no clipped interactive controls, and no
   touch target below 48dp.
