from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost
from tools.doodad_cli.scene_trace import run_flow_action


ROOT = Path(__file__).resolve().parents[1]


def perform(host: NativeHost, action: dict[str, object]) -> None:
    run_flow_action(host, action)


class CrossAppStressTests(unittest.TestCase):
    def test_two_full_hot_replacement_cycles_are_bounded(self) -> None:
        suite = json.loads(
            (ROOT / "apps" / "conformance-suite.json").read_text()
        )
        flows = json.loads(
            (ROOT / "apps" / "conformance-flows.json").read_text()
        )["flows"]
        packages = {
            entry["slug"]: build_and_stage(
                ROOT, ROOT / "apps" / entry["slug"]
            )
            for entry in suite["apps"]
        }

        initial_metrics: dict[str, tuple[int, int, int, int]] = {}
        final_frame_hashes: set[bytes] = set()
        host = NativeHost(ROOT)
        try:
            for cycle in range(2):
                for entry in suite["apps"]:
                    slug = entry["slug"]
                    with self.subTest(cycle=cycle, app=slug):
                        host.start_wasm(packages[slug].wasm)
                        semantic = json.loads(
                            host.semantic_snapshot()
                        )
                        self.assertEqual(semantic["app"], slug)
                        metrics = (
                            host.mounted_node_count(),
                            host.mounted_event_count(),
                            host.lvgl_object_count(),
                            host.lvgl_max_depth(),
                        )
                        if cycle == 0:
                            initial_metrics[slug] = metrics
                        else:
                            self.assertEqual(
                                metrics, initial_metrics[slug]
                            )

                        for action in flows[slug]:
                            perform(host, action)
                        semantic = json.loads(
                            host.semantic_snapshot()
                        )
                        self.assertEqual(semantic["app"], slug)
                        self.assertLessEqual(
                            host.mounted_node_count(), 64
                        )
                        self.assertLessEqual(
                            host.lvgl_object_count(), 96
                        )
                        self.assertLessEqual(
                            host.lvgl_max_depth(), 10
                        )
                        final_frame_hashes.add(
                            host.framebuffer_rgb565()
                        )
        finally:
            host.close()

        self.assertEqual(len(initial_metrics), 20)
        self.assertGreaterEqual(len(final_frame_hashes), 18)


if __name__ == "__main__":
    unittest.main()
