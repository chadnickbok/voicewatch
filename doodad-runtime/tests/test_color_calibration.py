from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "color_calibration"))

from analyze import apply_affine, fit_affine, patches  # noqa: E402
from apply import load_profile, require_capture_setting  # noqa: E402


class ColorCalibrationTests(unittest.TestCase):
    def test_target_is_a_complete_eight_by_five_rgb565_grid(self) -> None:
        target = patches()
        self.assertEqual(len(target), 40)
        self.assertEqual(
            {(patch.row, patch.column) for patch in target},
            {(row, column) for row in range(5) for column in range(8)},
        )
        self.assertEqual(target[0].name, "bar_white")
        self.assertEqual(target[0].expected, (255, 255, 255))
        self.assertIn(
            ("bar_black", (0, 0, 0)),
            {(patch.name, patch.expected) for patch in target},
        )

    def test_affine_fit_recovers_identity_for_reference_samples(self) -> None:
        expected = [patch.expected for patch in patches()]
        matrix = fit_affine(expected, expected)
        for row in range(3):
            for column in range(4):
                target = 1.0 if row == column else 0.0
                self.assertAlmostEqual(matrix[row][column], target, places=8)
        corrected = apply_affine(matrix, (17, 103, 241))
        for actual, wanted in zip(
            corrected,
            (17 / 255.0, 103 / 255.0, 241 / 255.0),
            strict=True,
        ):
            self.assertAlmostEqual(actual, wanted, places=8)

    def test_profile_validation_and_capture_setting_guard(self) -> None:
        profile_path = (
            ROOT / "config" / "capture" / "streamcam-cores3-sharp.json"
        )
        profile = load_profile(profile_path)
        require_capture_setting(profile, "exposure", 16, "exposure")
        require_capture_setting(profile, "gain", 58, "gain")
        require_capture_setting(
            profile,
            "white_balance_temperature",
            4000,
            "white-balance temperature",
        )
        require_capture_setting(profile, "focus_mode", "auto", "focus mode")
        with self.assertRaisesRegex(ValueError, "gain mismatch"):
            require_capture_setting(profile, "gain", 20, "gain")


if __name__ == "__main__":
    unittest.main()
