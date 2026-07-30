from __future__ import annotations

import copy
import hashlib
import json
import unittest
from pathlib import Path

from tools.doodad_cli.appspec import compile_to_ui_v0, validate_appspec
from tools.doodad_cli.appspec_cbor import compile_canonical_cbor
from tools.doodad_cli.contract import DoodadError
from tools.doodad_cli.ui import validate_ui


ROOT = Path(__file__).resolve().parents[1]


class AppSpecTests(unittest.TestCase):
    def load(self, app: str) -> dict:
        return json.loads((ROOT / "apps" / app / "appspec.json").read_text())

    def test_reference_apps_validate_and_compile(self) -> None:
        for app in ("hello", "calories", "calculator", "workout", "voice"):
            document = self.load(app)
            stats = validate_appspec(document)
            self.assertGreaterEqual(stats.nodes, 2)
            self.assertLessEqual(stats.maximum_depth, 12)
            validate_ui(compile_to_ui_v0(document))

    def test_reference_apps_compile_to_bounded_canonical_cbor(self) -> None:
        for app in ("hello", "calories", "calculator", "workout", "voice"):
            payload = compile_canonical_cbor(self.load(app))
            self.assertLessEqual(len(payload), 4096)
            self.assertEqual(payload[0], 0xA3)

    def test_hello_cbor_wire_fixture_is_stable(self) -> None:
        payload = compile_canonical_cbor(self.load("hello"))
        self.assertEqual(
            hashlib.sha256(payload).hexdigest(),
            "c1954e5406deeaf400e3b8371e8b9e491a318f3dd9e6b989005360a842fd8276",
        )

    def test_capability_component_hash_is_reproducible(self) -> None:
        capability = json.loads(
            (ROOT / "contracts" / "capabilities-cores3.json").read_text()
        )
        payload = (
            f"appspec:{capability['appspec']}\n"
            + "\n".join(sorted(capability["components"]))
            + "\n"
        )
        self.assertEqual(
            capability["componentSetHash"],
            f"sha256:{hashlib.sha256(payload.encode()).hexdigest()}",
        )

    def test_raw_styling_is_not_an_escape_hatch(self) -> None:
        document = self.load("calories")
        document["screen"]["props"]["children"][1]["props"]["color"] = "#ff0000"
        with self.assertRaisesRegex(DoodadError, "unsupported semantic props"):
            validate_appspec(document)

    def test_interactive_nodes_require_useful_semantics(self) -> None:
        document = self.load("workout")
        del document["screen"]["props"]["children"][1]["semantics"]
        with self.assertRaisesRegex(DoodadError, "semantics.label"):
            validate_appspec(document)

    def test_duplicate_ids_are_rejected_before_rendering(self) -> None:
        document = self.load("calories")
        children = document["screen"]["props"]["children"]
        duplicate = copy.deepcopy(children[0])
        children.append(duplicate)
        with self.assertRaisesRegex(DoodadError, "duplicate"):
            validate_appspec(document)

    def test_only_one_primary_scroll_axis_is_allowed(self) -> None:
        document = {
            "schema_version": 1,
            "app_id": "scroll_test",
            "screen": {
                "id": "screen",
                "type": "screen",
                "props": {
                    "children": [
                        {
                            "id": "a",
                            "type": "scroll",
                            "props": {"children": []},
                        },
                        {
                            "id": "b",
                            "type": "scroll",
                            "props": {"children": []},
                        },
                    ]
                },
            },
        }
        with self.assertRaisesRegex(DoodadError, "one primary scroll"):
            validate_appspec(document)

    def test_typed_bindings_validate_and_preview_without_raw_expressions(self) -> None:
        document = self.load("calories")
        total = document["screen"]["props"]["children"][0]
        total["props"]["text"] = {
            "bind": "shared.nutrition.total",
            "format": {"kind": "number", "unit": "kcal"},
        }
        total["visible"] = {
            "bind": "shared.nutrition.total",
            "predicate": {"op": "greater_than", "value": 0},
        }
        validate_appspec(document)
        preview = compile_to_ui_v0(document)
        self.assertIn(
            "{shared.nutrition.total}",
            str(preview),
        )

    def test_bindings_reject_expression_escape_hatches(self) -> None:
        document = self.load("calories")
        total = document["screen"]["props"]["children"][0]
        total["props"]["text"] = {
            "bind": "shared.nutrition.total",
            "expression": "value * arbitrary_code()",
        }
        with self.assertRaisesRegex(DoodadError, "invalid binding shape"):
            validate_appspec(document)


if __name__ == "__main__":
    unittest.main()
