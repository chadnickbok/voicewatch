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
                "button": 148,
                "card": 71,
                "keypad": 2,
                "live_card": 2,
                "progress": 2,
                "screen": 83,
                "stepper": 4,
                "text": 141,
            },
        )
        self.assertEqual(
            document["authored"]["patterns"],
            {
                "action_list": 6,
                "countdown": 1,
                "empty": 1,
                "keypad": 2,
                "metric_control": 4,
                "progress_dashboard": 1,
                "status_detail": 67,
                "weather_hero": 1,
            },
        )
        self.assertEqual(
            document["runtime"]["accepted_operations"],
            114,
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
