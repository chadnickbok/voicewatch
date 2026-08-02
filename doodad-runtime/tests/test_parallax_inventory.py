from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "reference" / "parallax-corpus-inventory.json"


class ParallaxInventoryTests(unittest.TestCase):
    def test_inventory_freezes_the_exact_initial_corpus(self) -> None:
        document = json.loads(INVENTORY.read_text())
        self.assertEqual(document["suite"]["app_count"], 20)
        self.assertEqual(document["suite"]["document_count"], 87)
        self.assertEqual(
            document["authored"]["component_counts"],
            {
                "button": 146,
                "canvas": 2,
                "card": 19,
                "chart": 2,
                "column": 22,
                "icon": 23,
                "image": 11,
                "keypad": 3,
                "live_card": 46,
                "progress": 7,
                "row": 81,
                "screen": 87,
                "scroll": 15,
                "stepper": 4,
                "surface": 29,
                "text": 203,
                "toggle": 9,
                "voice_orb": 1,
            },
        )
        self.assertEqual(
            document["authored"]["patterns"],
            {
                "action_list": 5,
                "calendar_agenda": 5,
                "camera_remote": 5,
                "canvas_game": 2,
                "countdown": 1,
                "empty": 1,
                "keypad": 1,
                "notification_stack": 6,
                "nutrition_dashboard": 1,
                "nutrition_quick_add": 1,
                "nutrition_review": 1,
                "task_list": 4,
                "live_action_detail": 43,
                "media_player": 5,
                "wallet_qr": 1,
                "voice_ready": 1,
                "workout_rest": 1,
                "workout_set": 2,
                "workout_summary": 1,
            },
        )
        self.assertEqual(
            document["runtime"]["accepted_operations"],
            112,
        )
        self.assertEqual(document["runtime"]["checkpoints"], 103)

    def test_inventory_generator_reports_current(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                "tools/generate_parallax_inventory.py",
                "--check",
            ],
            cwd=ROOT,
            env={**os.environ, "PYTHONPATH": "tools"},
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
