from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ConformanceEvidenceTests(unittest.TestCase):
    def test_all_apps_have_bounded_semantic_resource_evidence(self) -> None:
        suite = json.loads(
            (ROOT / "apps" / "conformance-suite.json").read_text()
        )
        for entry in suite["apps"]:
            slug = entry["slug"]
            with self.subTest(app=slug):
                report = json.loads(
                    (
                        ROOT
                        / "evidence"
                        / "conformance"
                        / f"{slug}.json"
                    ).read_text()
                )
                self.assertEqual(report["schema"], 1)
                self.assertEqual(report["app"]["slug"], slug)
                summary = report["summary"]
                budgets = report["budgets"]
                self.assertLessEqual(
                    summary["module_bytes"],
                    budgets["maximum_module_bytes"],
                )
                self.assertLessEqual(
                    summary["maximum_appspec_bytes"],
                    budgets["maximum_appspec_bytes"],
                )
                self.assertLessEqual(
                    summary["maximum_semantic_nodes"],
                    budgets["maximum_semantic_nodes"],
                )
                self.assertLessEqual(
                    summary["maximum_lvgl_objects"],
                    budgets["maximum_lvgl_objects"],
                )
                self.assertLessEqual(
                    summary["maximum_lvgl_depth"],
                    budgets["maximum_lvgl_depth"],
                )
                self.assertGreaterEqual(
                    summary["distinct_semantic_states"], 2
                )
                self.assertGreater(summary["total_changed_pixels"], 0)
                for stage in report["stages"]:
                    tree = stage["semantic_tree"]
                    self.assertEqual(tree["app"], slug)
                    self.assertEqual(
                        stage["mounted_nodes"], len(tree["nodes"])
                    )
                    self.assertEqual(
                        tree["nodes"][0]["role"], "screen"
                    )
                    self.assertTrue(
                        any(
                            node["role"] == "button"
                            for node in tree["nodes"]
                        )
                        or stage is report["stages"][-1]
                    )


if __name__ == "__main__":
    unittest.main()
