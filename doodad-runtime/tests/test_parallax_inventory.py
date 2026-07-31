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
                "button": 133,
                "card": 75,
                "column": 5,
                "keypad": 2,
                "live_card": 3,
                "progress": 2,
                "row": 1,
                "screen": 83,
                "scroll": 15,
                "stepper": 4,
                "text": 138,
                "toggle": 9,
            },
        )
        self.assertEqual(
            document["authored"]["patterns"],
            {
                "calendar_agenda": 5,
                "countdown": 1,
                "empty": 1,
                "keypad": 2,
                "metric_control": 1,
                "notification_stack": 6,
                "task_list": 4,
                "workout_rest": 1,
                "workout_set": 2,
                "workout_summary": 1,
                "progress_dashboard": 1,
                "status_detail": 57,
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
