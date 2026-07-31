from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.doodad_cli.native import NativeHost
from tools.doodad_cli.parallax_contract import (
    document_sha256,
    validate_node_evidence,
)
from tools.doodad_cli.perfect_render import (
    capture_lvgl_entry,
    capture_lvgl_suite,
    entry_output_directory,
    resolve_suite_entries,
)


ROOT = Path(__file__).resolve().parents[1]
SUITE_PATH = ROOT / "reference" / "perfect-render-suite.json"


def _tree_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


class PerfectRenderSuiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = json.loads(
            (ROOT / "apps" / "conformance-suite.json").read_text()
        )["apps"]
        cls.suite = json.loads(SUITE_PATH.read_text())
        cls.selections = resolve_suite_entries(SUITE_PATH)

    def test_suite_selects_exactly_twenty_shared_initial_snapshots(self) -> None:
        expected_slugs = [app["slug"] for app in self.catalog]
        self.assertEqual(len(self.suite["entries"]), 20)
        self.assertEqual(
            [entry["app_slug"] for entry in self.suite["entries"]],
            expected_slugs,
        )
        self.assertEqual(
            [selection.entry["app_slug"] for selection in self.selections],
            expected_slugs,
        )

        for selection in self.selections:
            with self.subTest(app=selection.entry["app_slug"]):
                entry = selection.entry
                target = selection.target_entry
                self.assertEqual(entry["sequence"], 0)
                self.assertEqual(target["sequence"], 0)
                self.assertEqual(target["scene_revision"], 1)
                self.assertEqual(entry["capture_phase"], "resting")
                self.assertEqual(entry["profile_id"], "watch_square_240")
                self.assertEqual(entry["compose"]["mode"], "host")
                self.assertEqual(entry["lvgl"]["mode"], "simulator")
                if entry["app_slug"] in {
                    "timer",
                    "weather",
                    "notifications",
                    "tasks",
                    "calendar",
                    "workout",
                    "calories",
                    "voice-notes",
                    "medication",
                }:
                    self.assertEqual(entry["review"]["status"], "approved")
                    self.assertEqual(
                        entry["review"]["reviewed_at"],
                        "2026-07-30",
                    )
                else:
                    self.assertEqual(entry["review"], {"status": "pending"})
                self.assertEqual(
                    target["after_snapshot_sha256"],
                    entry["snapshot_sha256"],
                )
                self.assertEqual(
                    document_sha256(selection.snapshot),
                    entry["snapshot_sha256"],
                )
                self.assertIsNotNone(selection.checkpoint)
                self.assertEqual(selection.checkpoint["stage_index"], 0)
                self.assertEqual(
                    selection.checkpoint["snapshot_sha256"],
                    entry["snapshot_sha256"],
                )
                self.assertEqual(len(selection.operations), 1)
                self.assertEqual(selection.operations[0].kind, "mount")

    def test_one_entry_is_byte_deterministic_and_never_invokes_wasm(
        self,
    ) -> None:
        selection = self.selections[0]
        with tempfile.TemporaryDirectory(
            prefix="perfect-render-a-",
            dir=ROOT / "target",
        ) as first_temporary, tempfile.TemporaryDirectory(
            prefix="perfect-render-b-",
            dir=ROOT / "target",
        ) as second_temporary, mock.patch.object(
            NativeHost,
            "start_wasm",
            side_effect=AssertionError("perfect render must not start Wasm"),
        ):
            first_root = Path(first_temporary)
            second_root = Path(second_temporary)
            first = capture_lvgl_entry(ROOT, selection, first_root)
            second = capture_lvgl_entry(ROOT, selection, second_root)

            self.assertEqual(first, second)
            self.assertEqual(
                _tree_bytes(first_root),
                _tree_bytes(second_root),
            )
            self.assertEqual(first["attestations"]["wasm_call_count"], 0)
            self.assertTrue(
                first["attestations"]["suite_snapshot_hash_shared"]
            )
            self.assertTrue(
                first["attestations"]["renderer_snapshot_hash_shared"]
            )

            output = entry_output_directory(first_root, selection.entry)
            self.assertEqual(
                {path.name for path in output.iterdir()},
                {
                    "scene-snapshot.json",
                    "lvgl.rgb565le",
                    "lvgl-nodes.json",
                    "manifest.json",
                },
            )
            framebuffer = (output / "lvgl.rgb565le").read_bytes()
            self.assertEqual(
                len(framebuffer),
                NativeHost.WIDTH * NativeHost.HEIGHT * 2,
            )
            evidence = json.loads(
                (output / "lvgl-nodes.json").read_text()
            )
            validate_node_evidence(evidence)
            for artifact in first["artifacts"].values():
                payload = (output / artifact["path"]).read_bytes()
                self.assertEqual(len(payload), artifact["bytes"])
                self.assertEqual(
                    hashlib.sha256(payload).hexdigest(),
                    artifact["sha256"],
                )

    def test_all_twenty_initial_scenes_capture_through_lvgl_replay(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="perfect-render-20-",
            dir=ROOT / "target",
        ) as temporary, mock.patch.object(
            NativeHost,
            "start_wasm",
            side_effect=AssertionError("perfect render must not start Wasm"),
        ):
            output_root = Path(temporary)
            manifests = capture_lvgl_suite(
                ROOT,
                SUITE_PATH,
                output_root,
            )
            self.assertEqual(len(manifests), 20)
            self.assertEqual(
                [
                    manifest["selection"]["app_slug"]
                    for manifest in manifests
                ],
                [app["slug"] for app in self.catalog],
            )

            for selection, manifest in zip(
                self.selections,
                manifests,
                strict=True,
            ):
                with self.subTest(app=selection.entry["app_slug"]):
                    self.assertEqual(
                        manifest["hashes"]["snapshot_sha256"],
                        selection.entry["snapshot_sha256"],
                    )
                    self.assertEqual(
                        manifest["attestations"]["wasm_call_count"],
                        0,
                    )
                    output = entry_output_directory(
                        output_root,
                        selection.entry,
                    )
                    self.assertEqual(
                        len((output / "lvgl.rgb565le").read_bytes()),
                        NativeHost.WIDTH * NativeHost.HEIGHT * 2,
                    )
                    self.assertTrue(
                        (output / "scene-snapshot.json").is_file()
                    )
                    self.assertTrue((output / "lvgl-nodes.json").is_file())
                    self.assertTrue((output / "manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
