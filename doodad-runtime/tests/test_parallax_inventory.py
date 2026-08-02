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
        self.assertEqual(document["suite"]["document_count"], 95)
        self.assertEqual(
            document["authored"]["component_counts"],
            {
                "button": 160,
                "canvas": 2,
                "card": 38,
                "chart": 2,
                "column": 22,
                "icon": 23,
                "image": 11,
                "keypad": 3,
                "live_card": 43,
                "progress": 10,
                "row": 84,
                "screen": 95,
                "scroll": 15,
                "stepper": 4,
                "surface": 29,
                "text": 214,
                "toggle": 9,
                "voice_orb": 1,
            },
        )
        self.assertEqual(
            document["authored"]["patterns"],
            {
                "action_list": 1,
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
                "live_action_detail": 43,
                "media_player": 5,
                "powerlifting": 12,
                "status_detail": 4,
                "task_list": 4,
                "wallet_qr": 1,
                "voice_ready": 1,
            },
        )
        self.assertEqual(
            document["runtime"]["accepted_operations"],
            124,
        )
        self.assertEqual(document["runtime"]["checkpoints"], 117)

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
