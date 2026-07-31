from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import DoodadError
from tools.doodad_cli.parallax_contract import (
    canonical_json_bytes,
    document_sha256,
    validate_node_evidence,
    validate_perfect_render_suite,
    validate_scene_snapshot,
    validate_scene_trace,
)


ROOT = Path(__file__).resolve().parents[1]
ZERO_HASH = "0" * 64
ONE_HASH = "1" * 64
TWO_HASH = "2" * 64


def snapshot_fixture() -> dict:
    return {
        "schema_version": 1,
        "app_id": "timer",
        "screen_id": "timer.home",
        "origin": "guest_appspec",
        "nodes": [
            {
                "id": "timer.home",
                "parent_id": None,
                "kind": "screen",
                "depth": 0,
                "child_count": 2,
                "visible": True,
                "enabled": True,
                "props": {
                    "gap": "sm",
                    "alignment": "stretch",
                },
                "semantics": {
                    "role": "screen",
                    "label": "Timer",
                },
                "actions": [],
            },
            {
                "id": "timer.value",
                "parent_id": "timer.home",
                "kind": "text",
                "depth": 1,
                "child_count": 0,
                "visible": True,
                "enabled": True,
                "props": {
                    "primary_text": "05:00",
                    "variant": "numeral",
                    "alignment": "center",
                    "max_lines": 1,
                },
                "semantics": {
                    "role": "text",
                    "label": "Five minutes",
                    "value": "05:00",
                },
                "actions": [],
            },
            {
                "id": "timer.start",
                "parent_id": "timer.home",
                "kind": "button",
                "depth": 1,
                "child_count": 0,
                "visible": True,
                "enabled": True,
                "props": {
                    "primary_text": "Start",
                    "variant": "filled",
                    "tone": "primary",
                    "size": "default",
                },
                "semantics": {
                    "role": "button",
                    "label": "Start timer",
                },
                "actions": [
                    {
                        "kind": "tap",
                        "action_id": "timer.start",
                    }
                ],
            },
        ],
    }


def trace_fixture(snapshot: dict | None = None) -> dict:
    resolved = snapshot if snapshot is not None else snapshot_fixture()
    snapshot_bytes = canonical_json_bytes(resolved)
    snapshot_hash = document_sha256(resolved)
    return {
        "schema_version": 1,
        "id": "timer.primary",
        "app": {
            "slug": "timer",
            "id": "dev.doodad.timer",
            "package_sha256": ZERO_HASH,
            "wasm_sha256": ONE_HASH,
            "manifest_sha256": TWO_HASH,
        },
        "environment": {
            "profile_id": "watch_square_240",
            "locale": "en-US",
            "timezone": "America/Los_Angeles",
            "font_scale_milli": 1000,
            "reduced_motion": False,
            "origin": "guest_appspec",
            "versions": {
                "wamr": "2.4.0",
                "lvgl": "9.5.0",
                "host_abi": "1",
                "appspec": "1",
                "component_set": "1",
                "simulator": "parallax-v1",
            },
            "hashes": {
                "interpretation_policy": ZERO_HASH,
                "theme": ONE_HASH,
                "font": TWO_HASH,
                "icons": ZERO_HASH,
                "simulator_build": ONE_HASH,
            },
        },
        "scenario_id": "timer.primary",
        "entries": [
            {
                "sequence": 0,
                "scenario_time_ms": 0,
                "cause": {
                    "kind": "start",
                },
                "outcome": "committed",
                "scene_revision": 1,
                "route_generation": 1,
                "screen_id": "timer.home",
                "before_snapshot_sha256": None,
                "after_snapshot_sha256": snapshot_hash,
                "mount": {
                    "path": "mounts/0000.cbor",
                    "sha256": ZERO_HASH,
                    "bytes": 128,
                },
                "snapshot": {
                    "path": "snapshots/0000.json",
                    "sha256": snapshot_hash,
                    "bytes": len(snapshot_bytes),
                },
            }
        ],
    }


def evidence_fixture(snapshot: dict | None = None) -> dict:
    resolved = snapshot if snapshot is not None else snapshot_fixture()
    return {
        "schema_version": 1,
        "snapshot_sha256": document_sha256(resolved),
        "capture_phase": {
            "id": "resting",
            "state": "resting",
            "animation_fraction_milli": 0,
        },
        "renderer": {
            "kind": "lvgl",
            "mode": "simulator",
            "version": "9.5.0",
            "build_sha256": ZERO_HASH,
        },
        "profile_id": "watch_square_240",
        "physical_width_px": 240,
        "physical_height_px": 240,
        "nodes": [
            {
                "id": "timer.home",
                "parent_id": None,
                "role": "screen",
                "label": "Timer",
                "value": "",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [],
                "bounds_px": {
                    "x": 0,
                    "y": 0,
                    "width": 240,
                    "height": 240,
                },
                "bounds_dp_q8_8": {
                    "x": 0,
                    "y": 0,
                    "width": 192 * 256,
                    "height": 192 * 256,
                },
                "token_roles": {
                    "background": "background",
                },
            },
            {
                "id": "timer.start",
                "parent_id": "timer.home",
                "role": "button",
                "label": "Start timer",
                "value": "",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [
                    {
                        "kind": "tap",
                        "action_id": "timer.start",
                    }
                ],
                "bounds_px": {
                    "x": 20,
                    "y": 170,
                    "width": 200,
                    "height": 52,
                },
                "bounds_dp_q8_8": {
                    "x": 16 * 256,
                    "y": 136 * 256,
                    "width": 160 * 256,
                    "height": 42 * 256,
                },
                "token_roles": {
                    "container_color": "primary",
                },
                "text": {
                    "line_count": 1,
                    "truncated": False,
                    "baselines_px": [201],
                },
            },
        ],
    }


def suite_fixture(snapshot: dict | None = None) -> dict:
    resolved = snapshot if snapshot is not None else snapshot_fixture()
    return {
        "schema_version": 1,
        "id": "perfect-render-20",
        "entries": [
            {
                "app_slug": "timer",
                "trace": "traces/timer/primary/trace.json",
                "sequence": 0,
                "snapshot_sha256": document_sha256(resolved),
                "capture_phase": "resting",
                "profile_id": "watch_square_240",
                "compose": {
                    "mode": "host",
                    "version": "wear-compose-1.6.2",
                },
                "lvgl": {
                    "mode": "simulator",
                    "version": "9.5.0",
                },
                "comparison_policy": "square-app-baseline",
                "review": {
                    "status": "pending",
                },
            }
        ],
    }


class ParallaxContractTests(unittest.TestCase):
    def test_contract_schema_files_are_valid_json_with_unique_ids(self) -> None:
        names = (
            "scene-snapshot-v1.schema.json",
            "scene-trace-v1.schema.json",
            "node-evidence-v1.schema.json",
            "perfect-render-suite-v1.schema.json",
        )
        identifiers = []
        for name in names:
            with self.subTest(schema=name):
                document = json.loads((ROOT / "contracts" / name).read_text())
                self.assertEqual(
                    document["$schema"],
                    "https://json-schema.org/draft/2020-12/schema",
                )
                identifiers.append(document["$id"])
        self.assertEqual(len(identifiers), len(set(identifiers)))

    def test_canonical_hash_is_independent_of_dictionary_order(self) -> None:
        left = {"b": [2, 3], "a": {"z": "é", "x": True}}
        right = {"a": {"x": True, "z": "é"}, "b": [2, 3]}
        self.assertEqual(canonical_json_bytes(left), canonical_json_bytes(right))
        self.assertEqual(document_sha256(left), document_sha256(right))

    def test_valid_parallax_documents_are_accepted(self) -> None:
        snapshot = snapshot_fixture()
        validate_scene_snapshot(snapshot)
        validate_scene_trace(trace_fixture(snapshot))
        validate_node_evidence(evidence_fixture(snapshot))
        validate_perfect_render_suite(suite_fixture(snapshot))

    def test_snapshot_rejects_inconsistent_child_count(self) -> None:
        snapshot = snapshot_fixture()
        snapshot["nodes"][0]["child_count"] = 1
        with self.assertRaisesRegex(DoodadError, "child_count"):
            validate_scene_snapshot(snapshot)

    def test_snapshot_rejects_unknown_kind_properties(self) -> None:
        snapshot = snapshot_fixture()
        snapshot["nodes"][1]["props"]["tone"] = "primary"
        with self.assertRaisesRegex(DoodadError, "unknown fields"):
            validate_scene_snapshot(snapshot)

    def test_snapshot_rejects_duplicate_ids(self) -> None:
        snapshot = snapshot_fixture()
        snapshot["nodes"][2]["id"] = "timer.value"
        with self.assertRaisesRegex(DoodadError, "duplicates"):
            validate_scene_snapshot(snapshot)

    def test_committed_trace_revision_must_advance(self) -> None:
        trace = trace_fixture()
        trace["entries"][0]["scene_revision"] = 0
        with self.assertRaisesRegex(DoodadError, "advance"):
            validate_scene_trace(trace)

    def test_rejected_trace_entry_cannot_change_snapshot(self) -> None:
        trace = trace_fixture()
        prior = trace["entries"][0]["after_snapshot_sha256"]
        trace["entries"].append(
            {
                "sequence": 1,
                "scenario_time_ms": 1,
                "cause": {
                    "kind": "semantic_action",
                    "node_id": "timer.start",
                    "action_id": "timer.start",
                    "event_kind": "tap",
                },
                "outcome": "rejected",
                "scene_revision": 1,
                "route_generation": 1,
                "screen_id": "timer.home",
                "before_snapshot_sha256": prior,
                "after_snapshot_sha256": TWO_HASH,
                "failure": "rejected by fixture",
            }
        )
        with self.assertRaisesRegex(DoodadError, "rejected outcome"):
            validate_scene_trace(trace)

    def test_evidence_parent_must_precede_child(self) -> None:
        evidence = evidence_fixture()
        evidence["nodes"][1]["parent_id"] = "missing.parent"
        with self.assertRaisesRegex(DoodadError, "must precede"):
            validate_node_evidence(evidence)

    def test_suite_rejects_duplicate_capture_identity(self) -> None:
        suite = suite_fixture()
        suite["entries"].append(copy.deepcopy(suite["entries"][0]))
        with self.assertRaisesRegex(DoodadError, "duplicates"):
            validate_perfect_render_suite(suite)


if __name__ == "__main__":
    unittest.main()
