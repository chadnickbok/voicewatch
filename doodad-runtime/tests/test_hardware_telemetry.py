from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.capture_hardware_telemetry import main, parse_display_line, summarize


class HardwareTelemetryTests(unittest.TestCase):
    def test_parse_and_summarize_complete_display_samples(self) -> None:
        first = parse_display_line(
            "I doodad: [display] fps=1.0 frames=2 flushes=3 pixels=57600 "
            "avg_render_us=4100 max_render_us=5200 avg_flush_us=900 "
            "max_flush_us=1100 touch_presses=0 objects=42 "
            "internal_free=200000 internal_min=190000 internal_largest=150000 "
            "psram_free=8000000 psram_min=7900000 psram_largest=7800000 "
            "transfer=synchronous"
        )
        second = parse_display_line(
            "I doodad: [display] fps=0.0 frames=0 flushes=0 pixels=0 "
            "avg_render_us=0 max_render_us=0 avg_flush_us=0 max_flush_us=0 "
            "touch_presses=1 objects=42 internal_free=198000 "
            "internal_min=188000 internal_largest=148000 psram_free=7990000 "
            "psram_min=7890000 psram_largest=7790000 transfer=synchronous"
        )
        self.assertIsNotNone(first)
        self.assertIsNotNone(second)
        report = summarize([first, second])  # type: ignore[list-item]
        self.assertEqual(report["frames_total"], 2)
        self.assertEqual(report["max_render_us"], 5200)
        self.assertEqual(report["lifetime_frames"], 2)
        self.assertEqual(report["internal_free_min"], 198000)
        self.assertEqual(report["psram_largest_min"], 7790000)
        self.assertEqual(report["touch_presses_total"], 1)

    def test_incomplete_legacy_line_is_rejected(self) -> None:
        self.assertIsNone(
            parse_display_line(
                "I doodad: [display] fps=0.0 frames=0 transfer=synchronous"
            )
        )

    def test_failed_capture_preserves_raw_serial_log(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "capture"
            with (
                patch(
                    "sys.argv",
                    [
                        "capture_hardware_telemetry.py",
                        "--port",
                        "/dev/null",
                        "--duration",
                        "2",
                        "--output",
                        str(output),
                    ],
                ),
                patch(
                    "tools.capture_hardware_telemetry.capture_serial",
                    return_value="boot failed\n",
                ),
            ):
                self.assertEqual(main(), 1)
            self.assertEqual(
                (output / "serial.log").read_text(encoding="utf-8"),
                "boot failed\n",
            )
            self.assertTrue((output / "capture-error.txt").is_file())


if __name__ == "__main__":
    unittest.main()
