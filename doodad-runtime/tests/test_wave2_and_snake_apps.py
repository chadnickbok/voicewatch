from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class WaveTwoAndSnakeTests(unittest.TestCase):
    def run_app(self, slug: str, callback) -> None:
        package = build_and_stage(ROOT, ROOT / "apps" / slug)
        native = NativeHost(ROOT)
        try:
            native.start_wasm(package.wasm)
            callback(native)
        finally:
            native.close()

    def test_calorie_stepper_and_voice_review_commit_atomically(self) -> None:
        def flow(native: NativeHost) -> None:
            native.click_button("Add food")
            self.assertEqual(
                native.node_text("calories.quick.amount"), "100 kcal"
            )
            native.click_button("+")
            self.assertEqual(
                native.node_text("calories.quick.amount"), "150 kcal"
            )
            native.click_button("Add")
            self.assertEqual(native.node_text("today.total"), "1,570 kcal")

            native.click_button("Add food")
            native.click_button("Voice")
            self.assertEqual(
                native.node_text("calories.review.summary"), "650 kcal"
            )
            native.click_button("Save")
            self.assertEqual(native.node_text("today.total"), "2,220 kcal")

        self.run_app("calories", flow)

    def test_workout_set_commit_rest_and_session_summary(self) -> None:
        def flow(native: NativeHost) -> None:
            self.assertEqual(
                native.node_text("powerlifting.today.hero"), "HEAVY\nDAY"
            )
            native.click_button("START WORKOUT")
            self.assertEqual(
                native.node_text("powerlifting.session.count"), "0 OF 14"
            )
            native.dispatch_semantic_action(
                "powerlifting.session.squat",
                "workout.choose.exercise",
                "tap",
            )
            native.dispatch_semantic_action(
                "powerlifting.exercise-picker.back-squat",
                "workout.exercise.back-squat",
                "tap",
            )
            native.click_button("BEGIN SQUAT")
            self.assertTrue(
                native.node_text("powerlifting.active-set.target").startswith(
                    "140"
                )
            )
            native.dispatch_semantic_action(
                "powerlifting.active-set.target",
                "workout.edit.weight",
                "tap",
            )
            native.dispatch_semantic_action(
                "powerlifting.weight-editor.value",
                "workout.weight",
                "value_committed",
                145,
            )
            self.assertEqual(
                native.node_text("powerlifting.weight-editor.value"),
                "145 KG",
            )
            native.click_button("DONE")
            native.click_button("COMPLETE SET")
            native.dispatch_semantic_action(
                "powerlifting.set-result.reps",
                "workout.reps",
                "value_committed",
                3,
            )
            native.click_button("SAVE SET")
            self.assertEqual(
                native.node_text("powerlifting.missed-set.label"),
                "SET MISSED",
            )
            native.click_button("135 NEXT")
            self.assertEqual(
                native.node_text("powerlifting.rest.time"), "2:41"
            )
            native.dispatch_semantic_action(
                "powerlifting.rest.next", "workout.plates", "tap"
            )
            native.click_button("READY")
            native.dispatch_semantic_action(
                "powerlifting.active-set.complete",
                "workout.switch.preview",
                "long_press",
            )
            native.dispatch_semantic_action(
                "powerlifting.exercise-switcher.bench",
                "workout.finish",
                "long_press",
            )
            self.assertEqual(
                native.node_text("powerlifting.summary.sets"), "14 SETS"
            )

        self.run_app("workout", flow)

    def test_snake_has_deterministic_playable_game_state(self) -> None:
        def flow(native: NativeHost) -> None:
            def canvas_display_list() -> str:
                snapshot = json.loads(native.scene_snapshot())
                return next(
                    node["props"]["display_list"]
                    for node in snapshot["nodes"]
                    if node["id"] == "snake.game.canvas"
                )

            initial = canvas_display_list()
            self.assertTrue(initial.startswith("v1|C0|R1"))
            self.assertEqual(native.node_text("snake.game.score"), "0")

            native.click_button("GO")
            moved = canvas_display_list()
            self.assertNotEqual(moved, initial)
            native.click_button("GO")
            self.assertEqual(native.node_text("snake.game.score"), "1")

            native.click_button("R")
            self.assertEqual(native.node_text("snake.game.score"), "1")
            self.assertNotEqual(
                canvas_display_list(),
                moved,
            )

        self.run_app("snake", flow)


if __name__ == "__main__":
    unittest.main()
