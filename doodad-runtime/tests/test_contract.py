from __future__ import annotations

import copy
import unittest
from pathlib import Path

from tools.doodad_cli.contract import (
    DoodadError,
    load_abi,
    read_json,
    validate_manifest,
)
from tools.doodad_cli.ui import validate_ui


ROOT = Path(__file__).resolve().parents[1]


class ManifestContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.path = ROOT / "apps" / "hello" / "manifest.json"
        self.manifest = read_json(self.path)
        self.abi = load_abi(ROOT)

    def test_hello_manifest_is_valid(self) -> None:
        validate_manifest(self.manifest, self.abi, self.path)

    def test_unknown_capability_is_rejected(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["capabilities"] = ["audio.raw"]
        with self.assertRaisesRegex(DoodadError, "does not define capability"):
            validate_manifest(manifest, self.abi, self.path)

    def test_non_string_capability_is_a_contract_error(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["capabilities"] = [{"display": "text"}]
        with self.assertRaisesRegex(DoodadError, "only strings"):
            validate_manifest(manifest, self.abi, self.path)

    def test_unknown_manifest_field_is_rejected(self) -> None:
        manifest = copy.deepcopy(self.manifest)
        manifest["surprise"] = True
        with self.assertRaisesRegex(DoodadError, "unknown fields"):
            validate_manifest(manifest, self.abi, self.path)


class DeclarativeUiTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ui = read_json(ROOT / "apps" / "hello" / "ui.json")

    def test_hello_ui_is_valid(self) -> None:
        validate_ui(self.ui)

    def test_root_must_be_a_stack(self) -> None:
        document = {
            "schema_version": 0,
            "root": {"type": "text", "text": "nope"},
        }
        with self.assertRaisesRegex(DoodadError, "root must be a stack"):
            validate_ui(document)

    def test_progress_cannot_exceed_maximum(self) -> None:
        document = copy.deepcopy(self.ui)
        document["root"]["children"][2]["value"] = 2
        with self.assertRaisesRegex(DoodadError, "0 <= value <= maximum"):
            validate_ui(document)

    def test_unknown_node_is_rejected(self) -> None:
        document = copy.deepcopy(self.ui)
        document["root"]["children"] = [{"type": "raw_framebuffer"}]
        with self.assertRaisesRegex(DoodadError, "unsupported UI node"):
            validate_ui(document)


if __name__ == "__main__":
    unittest.main()
