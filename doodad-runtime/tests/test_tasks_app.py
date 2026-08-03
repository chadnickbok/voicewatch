from __future__ import annotations

import json
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
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "TODAY  /  2 LEFT",
        )

        self.native.dispatch_semantic_action(
            "tasks.milk",
            "tasks.toggle.milk",
            "checked_changed",
            True,
        )
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "TODAY  /  1 LEFT",
        )
        self.assertTrue(self.checked("tasks.milk"))
        self.native.dispatch_semantic_action(
            "tasks.milk",
            "tasks.toggle.milk",
            "checked_changed",
            False,
        )
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "TODAY  /  2 LEFT",
        )
        self.assertFalse(self.checked("tasks.milk"))

        self.native.click_button("+ Add by voice")
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "TODAY  /  3 LEFT",
        )
        self.native.dispatch_semantic_action(
            "tasks.bananas",
            "tasks.toggle.bananas",
            "checked_changed",
            True,
        )
        self.assertEqual(
            self.native.node_text("tasks.list.heading"),
            "TODAY  /  2 LEFT",
        )
        self.assertTrue(self.checked("tasks.bananas"))

    def checked(self, node_id: str) -> bool:
        snapshot = json.loads(self.native.scene_snapshot())
        node = next(
            node for node in snapshot["nodes"] if node["id"] == node_id
        )
        return bool(node["props"]["checked"])


if __name__ == "__main__":
    unittest.main()
