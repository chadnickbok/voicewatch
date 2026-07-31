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
        self.assertEqual(document["suite"]["document_count"], 83)
        self.assertEqual(
            document["authored"]["component_counts"],
            {
                "button": 139,
                "card": 36,
                "column": 5,
                "image": 5,
                "keypad": 2,
                "live_card": 37,
                "progress": 7,
                "row": 42,
                "screen": 83,
                "scroll": 15,
                "stepper": 4,
                "text": 146,
                "toggle": 9,
                "voice_orb": 1,
            },
        )
        self.assertEqual(
            document["authored"]["patterns"],
            {
                "calendar_agenda": 5,
                "countdown": 1,
                "empty": 1,
                "keypad": 2,
                "notification_stack": 6,
                "nutrition_dashboard": 1,
                "nutrition_quick_add": 1,
                "nutrition_review": 1,
                "task_list": 4,
                "live_action_detail": 34,
                "media_player": 5,
                "voice_ready": 1,
                "workout_rest": 1,
                "workout_set": 2,
                "workout_summary": 1,
                "status_detail": 16,
                "weather_hero": 1,
            },
        )
        self.assertEqual(
            document["runtime"]["accepted_operations"],
            112,
        )
        self.assertEqual(document["runtime"]["checkpoints"], 105)

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
