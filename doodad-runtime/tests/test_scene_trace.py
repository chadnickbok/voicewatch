from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.doodad_cli.parallax_contract import canonical_json_bytes
from tools.doodad_cli.scene_trace import (
    load_trace_bundle,
    record_flow_trace,
    verify_trace_bundle_fresh,
)


ROOT = Path(__file__).resolve().parents[1]
TRACES = ROOT / "reference" / "traces"


class SceneTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.suite = json.loads(
            (ROOT / "apps" / "conformance-suite.json").read_text()
        )["apps"]
        cls.flows = json.loads(
            (ROOT / "apps" / "conformance-flows.json").read_text()
        )["flows"]

    def test_checked_in_traces_cover_every_decisive_flow_stage(self) -> None:
        entries = 0
        checkpoints = 0
        for app in self.suite:
            bundle = load_trace_bundle(
                TRACES / app["slug"] / "decisive"
            )
            entries += len(bundle.trace["entries"])
            checkpoints += len(bundle.checkpoints["checkpoints"])
            self.assertEqual(bundle.trace["app"]["slug"], app["slug"])
            self.assertEqual(
                len(bundle.checkpoints["checkpoints"]),
                len(self.flows[app["slug"]]) + 1,
            )
        self.assertEqual(len(self.suite), 20)
        self.assertEqual(entries, 124)
        self.assertEqual(
            checkpoints,
            sum(len(self.flows[app["slug"]]) + 1 for app in self.suite),
        )

    def test_decisive_actions_use_stable_semantic_identity(self) -> None:
        for slug, actions in self.flows.items():
            for index, action in enumerate(actions):
                with self.subTest(app=slug, action=index):
                    self.assertNotEqual(action["kind"], "click")
                    if action["kind"] != "semantic":
                        self.assertIn(action["kind"], {"advance", "deliver"})
                        continue
                    self.assertEqual(
                        {
                            "kind",
                            "node_id",
                            "action_id",
                            "event_kind",
                        },
                        set(action) - {"typed_value"},
                    )

    def test_every_checked_in_trace_replays_without_wasm(self) -> None:
        for app in self.suite:
            with self.subTest(app=app["slug"]):
                result = verify_trace_bundle_fresh(
                    ROOT,
                    TRACES / app["slug"] / "decisive",
                )
                self.assertTrue(result["passed"])
                self.assertEqual(result["wasm_calls"], 0)

    def test_ten_timer_recordings_are_byte_identical(self) -> None:
        documents: list[tuple[bytes, bytes]] = []
        with tempfile.TemporaryDirectory(
            prefix="parallax-determinism-",
            dir=ROOT / "target",
        ) as temporary:
            for index in range(10):
                bundle = record_flow_trace(
                    ROOT,
                    "timer",
                    self.flows["timer"],
                    Path(temporary) / str(index),
                )
                documents.append(
                    (
                        canonical_json_bytes(bundle.trace),
                        canonical_json_bytes(bundle.checkpoints),
                    )
                )
        self.assertEqual(len(set(documents)), 1)


if __name__ == "__main__":
    unittest.main()
