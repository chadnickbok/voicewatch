from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class GuestLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.timer = build_and_stage(ROOT, ROOT / "apps" / "timer")
        cls.weather = build_and_stage(ROOT, ROOT / "apps" / "weather")

    def test_timer_service_runs_while_display_sleeps(self) -> None:
        host = NativeHost(ROOT)
        try:
            host.start_wasm(self.timer.wasm)
            host.click_button("Start")
            sleeping_frame = host.framebuffer_rgb565()
            host.set_display_awake(False)
            self.assertFalse(host.display_awake())

            host.advance_time(60_000)
            semantic = json.loads(host.semantic_snapshot())
            status = next(
                node
                for node in semantic["nodes"]
                if node["id"] == "timer.status"
            )
            self.assertEqual(status["text"], "Timer complete · fired once")
            self.assertEqual(host.framebuffer_rgb565(), sleeping_frame)

            host.set_display_awake(True)
            self.assertTrue(host.display_awake())
            self.assertNotEqual(
                host.framebuffer_rgb565(), sleeping_frame
            )
            host.click_button("Dismiss")
        finally:
            host.close()

    def test_host_service_survives_guest_replacement(self) -> None:
        host = NativeHost(ROOT)
        try:
            host.start_wasm(self.timer.wasm)
            host.click_button("Start")
            host.start_wasm(self.weather.wasm)
            host.advance_time(60_000)

            host.start_wasm(self.timer.wasm)
            host.advance_time(0)
            semantic = json.loads(host.semantic_snapshot())
            status = next(
                node
                for node in semantic["nodes"]
                if node["id"] == "timer.status"
            )
            self.assertEqual(status["text"], "Timer complete · fired once")
            host.click_button("Dismiss")
        finally:
            host.close()


if __name__ == "__main__":
    unittest.main()
