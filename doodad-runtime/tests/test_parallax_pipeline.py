from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.doodad_cli.parallax_contract import document_sha256
from tools.doodad_cli.parallax_image import write_png_rgb888
from tools.doodad_cli.parallax_pipeline import (
    capture_compose_suite,
    compare_captured_suite,
    compose_renderer_build_sha256,
)
from tools.doodad_cli.perfect_render import (
    capture_lvgl_entry,
    entry_output_directory,
    resolve_suite_entries,
)


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "reference" / "perfect-render-suite.json"


class ParallaxPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.selection = resolve_suite_entries(
            SUITE,
            app_slug="timer",
        )[0]

    def test_compose_renderer_build_hash_is_stable_and_source_sensitive(
        self,
    ) -> None:
        first = compose_renderer_build_sha256(ROOT)
        second = compose_renderer_build_sha256(ROOT)
        self.assertRegex(first, r"^[0-9a-f]{64}$")
        self.assertEqual(first, second)

    def test_mocked_host_capture_builds_attested_pair_and_static_report(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="parallax-pipeline-",
            dir=ROOT / "target",
        ) as temporary:
            output_root = Path(temporary)
            capture_lvgl_entry(
                ROOT,
                self.selection,
                output_root,
            )
            capture = capture_compose_suite(
                ROOT,
                [self.selection],
                output_root,
                command_runner=_fake_compose_capture,
            )
            self.assertEqual(len(capture.manifests), 1)
            manifest = capture.manifests[0]
            self.assertEqual(
                manifest["selection"]["snapshot_sha256"],
                self.selection.entry["snapshot_sha256"],
            )
            self.assertEqual(
                manifest["renderer"]["kind"],
                "compose",
            )
            self.assertTrue(
                manifest["attestations"]["renderer_snapshot_hash_shared"]
            )

            report, contact_sheet = compare_captured_suite(
                ROOT,
                [self.selection],
                output_root,
            )
            self.assertTrue(report.html.is_file())
            self.assertTrue(report.json.is_file())
            self.assertTrue(contact_sheet.is_file())
            document = json.loads(report.json.read_text())
            self.assertEqual(document["summary"]["case_count"], 1)
            self.assertEqual(
                document["cases"][0]["snapshot_sha256"],
                self.selection.entry["snapshot_sha256"],
            )
            self.assertEqual(
                set(document["cases"][0]["quality"]),
                {"lvgl_product", "wear_compose_reference"},
            )
            case_root = output_root / "report" / "cases" / "timer"
            expected = {
                "reference.png",
                "candidate.png",
                "side_by_side.png",
                "overlay.png",
                "difference.png",
                "reference_rgb565.png",
                "reference_boundaries.png",
                "candidate_boundaries.png",
                "metrics.json",
                "review.json",
            }
            self.assertTrue(
                expected.issubset(
                    {path.name for path in case_root.iterdir()}
                )
            )


def _fake_compose_capture(
    command: list[str],
    _cwd: Path,
    _environment: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    manifest_argument = next(
        item for item in command if item.startswith("-Pparallax.manifest=")
    )
    build_argument = next(
        item
        for item in command
        if item.startswith("-Pparallax.rendererBuildSha256=")
    )
    manifest = Path(manifest_argument.split("=", 1)[1])
    build_sha256 = build_argument.split("=", 1)[1]
    for request in json.loads(manifest.read_text()):
        snapshot_path = Path(request["snapshot"])
        output_path = Path(request["output"])
        snapshot = json.loads(snapshot_path.read_text())
        raw = bytes((20, 24, 32)) * (240 * 240)
        write_png_rgb888(
            output_path,
            raw,
            width=240,
            height=240,
        )
        output_path.with_suffix(".rgb888").write_bytes(raw)
        output_path.with_suffix(".rgb888.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "width": 240,
                    "height": 240,
                    "stride_bytes": 720,
                    "pixel_format": "rgb888",
                    "byte_order": "r_g_b",
                    "bytes": 172800,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n"
        )
        evidence = {
            "schema_version": 1,
            "snapshot_sha256": document_sha256(snapshot),
            "capture_phase": {
                "id": "resting",
                "state": "resting",
                "animation_fraction_milli": 0,
            },
            "renderer": {
                "kind": "compose",
                "mode": "host",
                "version": "wear-compose-1.6.2",
                "build_sha256": build_sha256,
            },
            "profile_id": "watch_square_240",
            "physical_width_px": 240,
            "physical_height_px": 240,
            "nodes": [
                _evidence_node(node, index)
                for index, node in enumerate(snapshot["nodes"])
            ],
        }
        output_path.with_name(
            output_path.stem + ".node-evidence.json"
        ).write_text(
            json.dumps(
                evidence,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            + "\n"
        )
    return subprocess.CompletedProcess(command, 0, "fake capture", "")


def _evidence_node(node: dict, index: int) -> dict:
    if index == 0:
        px = {"x": 0, "y": 0, "width": 240, "height": 240}
    else:
        px = {
            "x": 10,
            "y": 10 + (index - 1) * 35,
            "width": 220,
            "height": 60,
        }
    dp = {
        coordinate: value * 1024 // 5
        for coordinate, value in px.items()
    }
    semantics = node["semantics"]
    evidence = {
        "id": node["id"],
        "parent_id": node["parent_id"],
        "role": semantics["role"],
        "label": semantics["label"],
        "value": semantics.get("value", ""),
        "state_description": semantics.get("state_description", ""),
        "visible": node["visible"],
        "enabled": node["enabled"],
        "actions": node["actions"],
        "bounds_px": px,
        "bounds_dp_q8_8": dp,
        "token_roles": {"component": f"material.{node['kind']}"},
    }
    for field in ("selected", "checked"):
        if field in node["props"]:
            evidence[field] = node["props"][field]
    return evidence


if __name__ == "__main__":
    unittest.main()
