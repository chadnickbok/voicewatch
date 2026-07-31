from __future__ import annotations

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
            native.click_button("Quick add")
            self.assertEqual(
                native.node_text("calories.quick.amount"), "100 kcal"
            )
            native.click_button("+")
            self.assertEqual(
                native.node_text("calories.quick.amount"), "150 kcal"
            )
            native.click_button("Add calories")
            self.assertEqual(native.node_text("today.total"), "1570 kcal")

            native.click_button("Quick add")
            native.click_button("Voice input")
            self.assertEqual(
                native.node_text("calories.review.summary"), "650 kcal"
            )
            native.click_button("Confirm record")
            self.assertEqual(native.node_text("today.total"), "2220 kcal")

        self.run_app("calories", flow)

    def test_workout_set_commit_rest_and_session_summary(self) -> None:
        def flow(native: NativeHost) -> None:
            self.assertEqual(native.node_text("active_set.weight"), "135 lb")
            native.click_button("+")
            native.click_button("+")
            self.assertEqual(native.node_text("active_set.weight"), "145 lb")
            native.click_button("Log set")
            self.assertEqual(native.node_text("workout.rest.time"), "1:00")
            native.click_button("Next set")
            self.assertEqual(
                native.node_text("workout.next.weight"), "145 lb"
            )
            native.click_button("Log set")
            self.assertEqual(
                native.node_text("workout.summary.sets"), "4 sets"
            )
            native.click_button("Again")
            self.assertEqual(native.node_text("active_set.weight"), "145 lb")

        self.run_app("workout", flow)

    def test_snake_has_deterministic_playable_game_state(self) -> None:
        def flow(native: NativeHost) -> None:
            native.click_button("Play Snake")
            initial = native.node_text("snake.game.board-top")
            self.assertIn("@", initial)
            self.assertEqual(native.node_text("snake.game.score"), "SCORE 0")

            native.click_button("Go")
            moved = native.node_text("snake.game.board-top")
            self.assertNotEqual(moved, initial)
            native.click_button("Go")
            self.assertEqual(native.node_text("snake.game.score"), "SCORE 1")

            native.click_button("R")
            self.assertEqual(native.node_text("snake.game.score"), "SCORE 1")
            native.click_button("New")
            self.assertEqual(native.node_text("snake.game.score"), "SCORE 0")
            native.click_button("Quit")
            self.assertEqual(native.node_text("snake.summary"), "Score 12")

        self.run_app("snake", flow)


if __name__ == "__main__":
    unittest.main()
