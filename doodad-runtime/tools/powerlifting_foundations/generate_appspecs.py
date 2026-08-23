#!/usr/bin/env python3
"""Generate the deterministic Powerlifting AppSpec screen set."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.appspec_cbor import compile_canonical_cbor  # noqa: E402


APP = ROOT / "apps" / "workout"
SCREENS = APP / "screens"


def text(identifier: str, value: str, style: str = "label") -> dict[str, Any]:
    return {
        "id": identifier,
        "type": "text",
        "props": {
            "text": value,
            "style": style,
            "max_lines": 1,
            "align": "center",
        },
    }


def card(
    identifier: str,
    title: str,
    body: str,
    *,
    tone: str = "neutral",
    action: str | None = None,
    extra_events: dict[str, str] | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "id": identifier,
        "type": "card",
        "props": {"title": title, "body": body, "tone": tone},
    }
    if action is not None:
        result["events"] = {"tap": action}
        result["semantics"] = {"label": title}
    if extra_events:
        result.setdefault("events", {}).update(extra_events)
        result.setdefault("semantics", {"label": title})
    return result


def button(
    identifier: str,
    label: str,
    action: str,
    *,
    tone: str = "primary",
    variant: str = "filled",
    size: str = "default",
    extra_events: dict[str, str] | None = None,
) -> dict[str, Any]:
    events = {"tap": action}
    events.update(extra_events or {})
    return {
        "id": identifier,
        "type": "button",
        "props": {
            "label": label,
            "tone": tone,
            "variant": variant,
            "size": size,
        },
        "events": events,
        "semantics": {"label": label},
    }


def progress(
    identifier: str,
    value: int,
    maximum: int,
    *,
    label: str = "Workout progress",
    tone: str = "primary",
) -> dict[str, Any]:
    return {
        "id": identifier,
        "type": "progress",
        "props": {
            "label": label,
            "value": value,
            "maximum": maximum,
            "style": "linear",
            "tone": tone,
        },
        "semantics": {
            "label": label,
            "value": f"{value} of {maximum}",
        },
    }


def stepper(
    identifier: str,
    label: str,
    value: int,
    unit: str,
    minimum: int,
    maximum: int,
    step: int,
    action: str,
) -> dict[str, Any]:
    return {
        "id": identifier,
        "type": "stepper",
        "props": {
            "label": label,
            "value": value,
            "unit": unit,
            "minimum": minimum,
            "maximum": maximum,
            "step": step,
        },
        "events": {"valueCommitted": action},
        "semantics": {"label": label, "value": f"{value} {unit}"},
    }


def row(identifier: str, children: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "id": identifier,
        "type": "row",
        "props": {"gap": "xs", "align": "stretch", "children": children},
    }


def document(screen_id: str, children: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "app_id": "workout",
        "screen": {
            "id": screen_id,
            "type": "screen",
            "props": {"gap": "none", "align": "stretch", "children": children},
        },
    }


def screens() -> dict[str, dict[str, Any]]:
    return {
        "today": document(
            "powerlifting.today",
            [
                text("powerlifting.today.kicker", "SAT / WEEK 4"),
                card(
                    "powerlifting.today.hero",
                    "HEAVY DAY",
                    "SQUAT / BENCH / DEADLIFT",
                    tone="primary",
                ),
                card(
                    "powerlifting.today.volume",
                    "14 SETS",
                    "3 LIFTS / TAP TO PLAN",
                    action="workout.manage",
                ),
                button(
                    "powerlifting.today.start",
                    "START WORKOUT",
                    "workout.start",
                    extra_events={"longPress": "workout.resume.preview"},
                ),
            ],
        ),
        "training-hub": document(
            "powerlifting.training-hub",
            [
                text("powerlifting.training-hub.title", "TRAINING", "title"),
                card(
                    "powerlifting.training-hub.plan",
                    "HEAVY DAY",
                    "14 SETS / 3 LIFTS",
                    tone="primary",
                    action="workout.plan.edit",
                ),
                card(
                    "powerlifting.training-hub.goal",
                    "142.5 / 150 KG",
                    "SQUAT 5RM / 95% TO GOAL",
                    tone="secondary",
                    action="workout.goal.edit",
                ),
                progress(
                    "powerlifting.training-hub.progress",
                    95,
                    100,
                    label="Strength goal progress",
                    tone="secondary",
                ),
                button(
                    "powerlifting.training-hub.done",
                    "DONE",
                    "workout.training.done",
                ),
            ],
        ),
        "workout-builder": document(
            "powerlifting.workout-builder",
            [
                text("powerlifting.workout-builder.title", "HEAVY DAY", "title"),
                text("powerlifting.workout-builder.count", "14 SETS", "caption"),
                card(
                    "powerlifting.workout-builder.squat",
                    "BACK SQUAT",
                    "5 X 5",
                    tone="primary",
                    action="workout.plan.exercise",
                ),
                card(
                    "powerlifting.workout-builder.remaining",
                    "BENCH 5 X 5  /  DEAD 4 X 5",
                    "2 MORE LIFTS",
                ),
                card(
                    "powerlifting.workout-builder.add",
                    "+ ADD EXERCISE",
                    "CHOOSE FROM RECENT LIFTS",
                    action="workout.plan.add",
                ),
                button(
                    "powerlifting.workout-builder.save",
                    "SAVE PLAN",
                    "workout.plan.save",
                ),
            ],
        ),
        "exercise-prescription": document(
            "powerlifting.exercise-prescription",
            [
                text(
                    "powerlifting.exercise-prescription.title",
                    "BACK SQUAT",
                    "title",
                ),
                stepper(
                    "powerlifting.exercise-prescription.sets",
                    "Work sets",
                    5,
                    "SETS",
                    1,
                    10,
                    1,
                    "workout.plan.sets",
                ),
                stepper(
                    "powerlifting.exercise-prescription.reps",
                    "Target reps",
                    5,
                    "REPS",
                    1,
                    20,
                    1,
                    "workout.plan.reps",
                ),
                card(
                    "powerlifting.exercise-prescription.context",
                    "140 KG / 3:00 / AUTO WARMUP",
                    "START / REST / WARMUPS",
                    tone="secondary",
                ),
                button(
                    "powerlifting.exercise-prescription.done",
                    "DONE",
                    "workout.plan.prescription.done",
                ),
            ],
        ),
        "strength-goal": document(
            "powerlifting.strength-goal",
            [
                text("powerlifting.strength-goal.title", "STRENGTH GOAL", "title"),
                card(
                    "powerlifting.strength-goal.lift",
                    "BACK SQUAT",
                    "5RM TARGET",
                    tone="primary",
                ),
                stepper(
                    "powerlifting.strength-goal.target",
                    "5RM target",
                    150,
                    "KG",
                    50,
                    300,
                    5,
                    "workout.goal.weight",
                ),
                text(
                    "powerlifting.strength-goal.context",
                    "142.5 CURRENT / 12 WEEKS",
                    "caption",
                ),
                button(
                    "powerlifting.strength-goal.save",
                    "SAVE GOAL",
                    "workout.goal.save",
                ),
            ],
        ),
        "session": document(
            "powerlifting.session",
            [
                text("powerlifting.session.title", "HEAVY DAY", "title"),
                text("powerlifting.session.count", "0 OF 14", "numeral"),
                progress("powerlifting.session.progress", 0, 14),
                card(
                    "powerlifting.session.squat",
                    "BACK SQUAT",
                    "5 SETS",
                    tone="primary",
                    action="workout.choose.exercise",
                ),
                card("powerlifting.session.bench", "BENCH PRESS", "5 SETS"),
                card("powerlifting.session.deadlift", "DEADLIFT", "4 SETS"),
                button("powerlifting.session.begin", "BEGIN SQUAT", "workout.begin"),
            ],
        ),
        "exercise-picker": document(
            "powerlifting.exercise-picker",
            [
                text("powerlifting.exercise-picker.title", "CHOOSE EXERCISE"),
                card(
                    "powerlifting.exercise-picker.back-squat",
                    "BACK SQUAT",
                    "RECENT / SELECTED",
                    tone="primary",
                    action="workout.exercise.back-squat",
                ),
                card(
                    "powerlifting.exercise-picker.front-squat",
                    "FRONT SQUAT",
                    "QUADS / UPRIGHT",
                    action="workout.exercise.front-squat",
                ),
                card(
                    "powerlifting.exercise-picker.paused-squat",
                    "PAUSED SQUAT",
                    "2 SECOND PAUSE",
                    action="workout.exercise.paused-squat",
                ),
            ],
        ),
        "active-set": document(
            "powerlifting.active-set",
            [
                text("powerlifting.active-set.exercise", "BACK SQUAT"),
                text("powerlifting.active-set.set", "SET 3 OF 5", "caption"),
                progress("powerlifting.active-set.progress", 3, 5),
                card(
                    "powerlifting.active-set.target",
                    "140 KG  X  5",
                    "TARGET SET",
                    tone="primary",
                    action="workout.edit.weight",
                ),
                card(
                    "powerlifting.active-set.previous",
                    "LAST  /  137.5 KG X 5 @8",
                    "PREVIOUS PERFORMANCE",
                ),
                button(
                    "powerlifting.active-set.complete",
                    "COMPLETE SET",
                    "workout.complete",
                    extra_events={"longPress": "workout.switch.preview"},
                ),
            ],
        ),
        "weight-editor": document(
            "powerlifting.weight-editor",
            [
                text("powerlifting.weight-editor.title", "WEIGHT"),
                stepper(
                    "powerlifting.weight-editor.value",
                    "Set weight",
                    140,
                    "KG",
                    20,
                    400,
                    5,
                    "workout.weight",
                ),
                card(
                    "powerlifting.weight-editor.plates",
                    "20 / 20 / 10 / 2.5",
                    "PLATES PER SIDE",
                    tone="secondary",
                ),
                button("powerlifting.weight-editor.done", "DONE", "workout.weight.done"),
            ],
        ),
        "set-result": document(
            "powerlifting.set-result",
            [
                card(
                    "powerlifting.set-result.summary",
                    "SET 3  /  140 KG",
                    "RECORD ACTUAL RESULT",
                    tone="primary",
                ),
                stepper(
                    "powerlifting.set-result.reps",
                    "Reps",
                    5,
                    "REPS",
                    0,
                    20,
                    1,
                    "workout.reps",
                ),
                row(
                    "powerlifting.set-result.rpe",
                    [
                        button(f"powerlifting.set-result.rpe-{value}", str(value), f"workout.rpe.{value}", size="compact", tone="neutral", variant="tonal")
                        for value in (7, 8, 9, 10)
                    ],
                ),
                button("powerlifting.set-result.save", "SAVE SET", "workout.save"),
            ],
        ),
        "rest": document(
            "powerlifting.rest",
            [
                text("powerlifting.rest.label", "REST"),
                text("powerlifting.rest.time", "2:41", "numeral"),
                progress("powerlifting.rest.progress", 19, 180),
                card(
                    "powerlifting.rest.next",
                    "NEXT  /  142.5 KG X 5",
                    "SET 4 OF 5 / HOLD TO EDIT LAST",
                    tone="primary",
                    action="workout.plates",
                    extra_events={"longPress": "workout.edit.result"},
                ),
                row(
                    "powerlifting.rest.controls",
                    [
                        button("powerlifting.rest.extend", "+30", "workout.rest.extend", size="compact"),
                        button("powerlifting.rest.skip", "SKIP", "workout.rest.skip", size="compact", tone="neutral", variant="tonal"),
                    ],
                ),
            ],
        ),
        "plate-loading": document(
            "powerlifting.plate-loading",
            [
                text("powerlifting.plate-loading.total", "142.5 KG", "numeral"),
                text("powerlifting.plate-loading.side", "61.25 PER SIDE", "caption"),
                card(
                    "powerlifting.plate-loading.diagram",
                    "20  20  10  10  1.25",
                    "EACH SIDE / 20 KG BAR",
                    tone="primary",
                ),
                button("powerlifting.plate-loading.ready", "READY", "workout.plates.ready"),
            ],
        ),
        "exercise-switcher": document(
            "powerlifting.exercise-switcher",
            [
                text("powerlifting.exercise-switcher.count", "3 / 14 SETS", "title"),
                card(
                    "powerlifting.exercise-switcher.squat",
                    "BACK SQUAT  /  3 OF 5",
                    "CURRENT",
                    action="workout.jump.squat",
                ),
                card(
                    "powerlifting.exercise-switcher.bench",
                    "BENCH PRESS  /  0 OF 5",
                    "JUMP / HOLD TO FINISH",
                    tone="primary",
                    action="workout.jump.bench",
                    extra_events={"longPress": "workout.finish"},
                ),
                card(
                    "powerlifting.exercise-switcher.deadlift",
                    "DEADLIFT  /  0 OF 4",
                    "JUMP HERE",
                    action="workout.jump.deadlift",
                ),
            ],
        ),
        "missed-set": document(
            "powerlifting.missed-set",
            [
                text("powerlifting.missed-set.label", "SET MISSED"),
                card(
                    "powerlifting.missed-set.actual",
                    "140 KG  X  3",
                    "TARGET 5 REPS @8",
                    tone="error",
                ),
                row(
                    "powerlifting.missed-set.options",
                    [
                        button("powerlifting.missed-set.drop", "135", "workout.missed.drop", size="compact", tone="secondary", variant="tonal"),
                        button("powerlifting.missed-set.log", "LOG 3", "workout.missed.log", size="compact", tone="neutral", variant="tonal"),
                        button("powerlifting.missed-set.retry", "RETRY", "workout.missed.retry", size="compact", tone="neutral", variant="tonal"),
                    ],
                ),
                button("powerlifting.missed-set.next", "135 NEXT", "workout.missed.next"),
            ],
        ),
        "summary": document(
            "powerlifting.summary",
            [
                text("powerlifting.summary.title", "WORKOUT COMPLETE"),
                row(
                    "powerlifting.summary.metrics",
                    [
                        text("powerlifting.summary.sets", "14 SETS", "title"),
                        text("powerlifting.summary.volume", "6,420 KG", "title"),
                        text("powerlifting.summary.duration", "1:07:32", "title"),
                    ],
                ),
                card(
                    "powerlifting.summary.pr",
                    "NEW 5RM  /  142.5 KG",
                    "BACK SQUAT",
                    tone="secondary",
                ),
                button("powerlifting.summary.done", "DONE", "workout.summary.done"),
            ],
        ),
        "resume": document(
            "powerlifting.resume",
            [
                text("powerlifting.resume.label", "WORKOUT PAUSED"),
                card(
                    "powerlifting.resume.state",
                    "142.5 KG  X  5",
                    "BACK SQUAT / SET 4 OF 5 / SAVED 24 SEC AGO",
                    tone="primary",
                ),
                button("powerlifting.resume.action", "RESUME", "workout.resume"),
                button(
                    "powerlifting.resume.discard",
                    "DISCARD",
                    "workout.discard",
                    tone="error",
                    variant="text",
                    size="compact",
                ),
            ],
        ),
    }


def main() -> int:
    SCREENS.mkdir(parents=True, exist_ok=True)
    generated = screens()
    for name, value in generated.items():
        json_path = APP / "appspec.json" if name == "today" else SCREENS / f"{name}.json"
        cbor_path = APP / "appspec.cbor" if name == "today" else SCREENS / f"{name}.cbor"
        json_path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        payload = compile_canonical_cbor(value)
        cbor_path.write_bytes(payload)
        print(f"{name:20s} {len(payload):4d} bytes  {json_path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
