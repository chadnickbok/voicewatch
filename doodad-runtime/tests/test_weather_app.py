from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class WeatherAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(ROOT, ROOT / "apps" / "weather")
        cls.native = NativeHost(ROOT)
        cls.native.start_wasm(cls.package.wasm)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.native.close()

    def refresh_and_deliver(self) -> None:
        self.native.click_button("Refresh")
        self.assertEqual(
            self.native.node_text("weather.status"),
            "Updating…",
        )
        self.assertEqual(
            self.native.node_text("weather.primary"),
            "Waiting…",
        )
        self.native.deliver_provider()

    def test_fresh_stale_offline_and_recovery_states(self) -> None:
        self.assertEqual(self.native.node_text("weather.summary"), "72°")
        self.assertEqual(
            self.native.node_text("weather.status"),
            "Updated now · revision 1",
        )

        self.refresh_and_deliver()
        self.assertEqual(self.native.node_text("weather.summary"), "72°")
        self.assertEqual(
            self.native.node_text("weather.status"),
            "Cached · 12 min old",
        )
        self.assertEqual(
            self.native.node_text("weather.forecast"),
            "Clear",
        )

        self.refresh_and_deliver()
        self.assertEqual(
            self.native.node_text("weather.status"),
            "Offline · cache 18 min",
        )
        self.assertEqual(
            self.native.node_text("weather.forecast"),
            "Offline",
        )

        self.refresh_and_deliver()
        self.assertEqual(self.native.node_text("weather.summary"), "71°")
        self.assertEqual(
            self.native.node_text("weather.status"),
            "Updated now · revision 2",
        )
        self.assertEqual(
            self.native.node_text("weather.primary"),
            "Refresh",
        )


if __name__ == "__main__":
    unittest.main()
