from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class TimerAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(ROOT, ROOT / "apps" / "timer")
        cls.native = NativeHost(ROOT)
        cls.native.start_wasm(cls.package.wasm)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.native.close()

    def test_exact_scheduler_drives_guest_without_remounting(self) -> None:
        self.assertEqual(self.native.node_text("timer.summary"), "1:00")
        self.assertEqual(self.native.node_text("timer.duration"), "1 min")
        self.native.click_button("+")
        self.assertEqual(self.native.node_text("timer.summary"), "2:00")
        self.assertEqual(self.native.node_text("timer.duration"), "2 min")
        self.native.click_button("+")
        self.assertEqual(self.native.node_text("timer.summary"), "3:00")
        self.assertEqual(self.native.node_text("timer.duration"), "3 min")

        self.native.click_button("Start")
        self.assertEqual(
            self.native.node_text("timer.status"),
            "Running in background",
        )
        self.native.advance_time(30_000)
        self.assertEqual(self.native.node_text("timer.summary"), "2:30")

        self.native.advance_time(150_000)
        self.assertEqual(self.native.node_text("timer.summary"), "TIME'S UP")
        self.assertEqual(
            self.native.node_text("timer.status"),
            "Timer complete · fired once",
        )

        # Re-polling the same exact deadline changes no firing ordinal.
        self.native.advance_time(0)
        self.assertEqual(
            self.native.node_text("timer.status"),
            "Timer complete · fired once",
        )
        self.native.click_button("Dismiss")
        self.assertEqual(self.native.node_text("timer.summary"), "3:00")
        self.assertEqual(
            self.native.node_text("timer.status"),
            "Ready · exact scheduler",
        )

    def test_cancel_prevents_a_later_fire(self) -> None:
        selected = self.native.node_text("timer.summary")
        self.native.click_button("Start")
        self.native.click_button("Cancel")
        self.native.advance_time(120_000)
        self.assertEqual(self.native.node_text("timer.summary"), selected)
        self.assertEqual(
            self.native.node_text("timer.status"),
            "Ready · exact scheduler",
        )


if __name__ == "__main__":
    unittest.main()
