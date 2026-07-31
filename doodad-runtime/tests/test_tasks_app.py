from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class TasksAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(ROOT, ROOT / "apps" / "tasks")
        cls.native = NativeHost(ROOT)
        cls.native.start_wasm(cls.package.wasm)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.native.close()

    def test_offline_crud_structural_insert_and_undo(self) -> None:
        self.native.click_button("Open groceries")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "GROCERIES · 2 LEFT",
        )

        self.native.click_button("○  Milk")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "GROCERIES · 1 LEFT",
        )
        self.assertEqual(
            self.native.node_text("tasks.milk"),
            "✓  Milk · undo",
        )
        self.native.click_button("✓  Milk · undo")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "GROCERIES · 2 LEFT",
        )

        self.native.click_button("+ Bananas")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "GROCERIES · 3 LEFT",
        )
        self.native.click_button("○  Bananas")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "GROCERIES · 2 LEFT",
        )

        self.native.click_button("Back")
        self.assertEqual(
            self.native.node_text("tasks.summary"),
            "Groceries · 2 left",
        )


if __name__ == "__main__":
    unittest.main()
