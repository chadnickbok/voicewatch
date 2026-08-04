from __future__ import annotations

import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "dual_capture", ROOT / "tools/dual_capture.py"
)
assert SPEC is not None and SPEC.loader is not None
dual_capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(dual_capture)


def fixture_frame(path: Path, *, include_watch: bool = True) -> None:
    draws = [
        "fill white rectangle 60,70 299,309",
        "fill #111827 rectangle 68,78 291,301",
        "fill white rectangle 267,82 270,85",
    ]
    if include_watch:
        draws.extend(
            [
                "fill white rectangle 450,60 689,299",
                "fill #172033 rectangle 458,68 681,291",
                "fill white rectangle 657,72 660,75",
                "fill white rectangle 664,72 667,75",
            ]
        )
    command = ["magick", "-size", "760x380", "xc:#050505"]
    for draw in draws:
        command.extend(["-draw", draw])
    command.append(str(path))
    subprocess.run(command, check=True)


class DualCaptureTest(unittest.TestCase):
    def test_detects_and_labels_swappable_marked_panels(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            frame = directory / "fixture.png"
            fixture_frame(frame)
            quads = dual_capture.detect_panel_quads(frame)
            panels = dual_capture.label_panels(frame, quads, directory / "crops")
            self.assertEqual(set(panels), {"cores3", "t-watch-s3"})
            self.assertLess(panels["cores3"][0][0], panels["t-watch-s3"][0][0])

    def test_missing_display_fails_registration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            frame = Path(temporary) / "missing.png"
            fixture_frame(frame, include_watch=False)
            with self.assertRaisesRegex(ValueError, "expected two"):
                dual_capture.detect_panel_quads(frame)

    def test_profile_requires_both_panels_and_matrices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / "profile.json"
            profile.write_text(json.dumps({"schema": dual_capture.SCHEMA, "panels": {}}))
            with self.assertRaisesRegex(ValueError, "incomplete"):
                dual_capture.load_profile(profile)

    def test_stale_profile_drift_uses_worst_corner(self) -> None:
        original = [[0, 0], [10, 0], [10, 10], [0, 10]]
        moved = [[0, 0], [10, 0], [22, 22], [0, 10]]
        self.assertGreater(dual_capture.quad_drift(original, moved), 10)

    def test_blank_crop_cannot_pass_quality(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            crop = Path(temporary) / "blank.png"
            subprocess.run(["magick", "-size", "240x240", "xc:black", str(crop)], check=True)
            with self.assertRaisesRegex(ValueError, "blank or unreadable"):
                dual_capture.quality(crop)


if __name__ == "__main__":
    unittest.main()
