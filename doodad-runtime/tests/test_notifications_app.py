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
        self.native.click_button("Maya · Dinner at 7?")
        self.assertEqual(
            self.native.node_text("notification.detail.message"),
            "Dinner at 7?",
        )

        self.native.click_button("Quick reply")
        self.assertEqual(
            self.native.node_text("notification.reply.heading"),
            "QUICK REPLY",
        )
        self.native.click_button("On my way")
        self.assertEqual(
            self.native.node_text("notification.replied.message"),
            "On my way",
        )

        self.native.click_button("Done")
        self.assertEqual(
            self.native.node_text("notifications.summary"),
            "1 unread",
        )
        self.native.click_button("Clear all")
        self.assertEqual(
            self.native.node_text("notifications.empty.heading"),
            "ALL CAUGHT UP",
        )


if __name__ == "__main__":
    unittest.main()
