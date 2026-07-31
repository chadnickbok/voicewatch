from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class NotificationAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(
            ROOT, ROOT / "apps" / "notifications"
        )
        cls.native = NativeHost(ROOT)
        cls.native.start_wasm(cls.package.wasm)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.native.close()

    def test_detail_reply_acknowledgement_and_synchronized_dismissal(
        self,
    ) -> None:
        self.assertEqual(
            self.native.node_text("notifications.summary"),
            "2 unread",
        )
        self.native.dispatch_semantic_action(
            "notifications.maya",
            "notification.open.maya",
            "tap",
        )
        self.assertEqual(
            self.native.node_text("notification.detail.message"),
            "Dinner at 7?",
        )

        self.native.dispatch_semantic_action(
            "notification.detail.reply",
            "notification.reply",
            "tap",
        )
        self.assertEqual(
            self.native.node_text("notification.reply.heading"),
            "Reply to Maya",
        )
        self.native.dispatch_semantic_action(
            "notification.reply.one",
            "notification.reply.on_my_way",
            "tap",
        )
        self.assertEqual(
            self.native.node_text("notification.replied.message"),
            "Coming",
        )

        self.native.dispatch_semantic_action(
            "notification.replied.done",
            "notification.reply.done",
            "tap",
        )
        self.assertEqual(
            self.native.node_text("notifications.summary"),
            "1 unread",
        )
        self.native.dispatch_semantic_action(
            "notifications.clear",
            "notification.clear",
            "tap",
        )
        self.assertEqual(
            self.native.node_text("notifications.empty.heading"),
            "ALL CAUGHT UP",
        )


if __name__ == "__main__":
    unittest.main()
