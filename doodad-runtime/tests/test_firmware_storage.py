from __future__ import annotations

import csv
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirmwareStorageTests(unittest.TestCase):
    def test_partition_table_uses_all_sixteen_megabytes(self) -> None:
        rows = {}
        with (ROOT / "firmware" / "partitions.csv").open() as stream:
            for row in csv.reader(
                line for line in stream if not line.lstrip().startswith("#")
            ):
                name, kind, subtype, offset, size, *_ = (
                    value.strip() for value in row
                )
                rows[name] = {
                    "type": kind,
                    "subtype": subtype,
                    "offset": int(offset, 0),
                    "size": int(size, 0),
                }

        self.assertEqual(rows["ota_0"]["size"], 3 * 1024 * 1024)
        self.assertEqual(rows["ota_1"]["size"], 3 * 1024 * 1024)
        self.assertEqual(rows["packages"]["subtype"], "fat")
        self.assertGreater(rows["packages"]["size"], 9 * 1024 * 1024)
        self.assertEqual(
            rows["packages"]["offset"] + rows["packages"]["size"],
            16 * 1024 * 1024,
        )

    def test_onboard_activation_precedes_sd_and_recovery(self) -> None:
        source = (ROOT / "firmware" / "main" / "src" / "main.cpp").read_text()
        onboard = source.index("load_onboard_app")
        sd = source.index("load_microsd_app")
        embedded = source.index("embedded_app_image")
        self.assertLess(onboard, sd)
        self.assertLess(sd, embedded)

        defaults = (
            ROOT / "firmware" / "sdkconfig.defaults"
        ).read_text()
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", defaults)

    def test_cores3_psram_is_configured_for_runtime_allocations(self) -> None:
        defaults = (
            ROOT / "firmware" / "sdkconfig.defaults"
        ).read_text()
        cores3_defaults = (
            ROOT / "firmware" / "boards" / "cores3" / "sdkconfig.defaults"
        ).read_text()
        watch_defaults = (
            ROOT / "firmware" / "boards" / "t-watch-s3" / "sdkconfig.defaults"
        ).read_text()
        self.assertIn("CONFIG_SPIRAM=y", defaults)
        self.assertIn("CONFIG_SPIRAM_MODE_QUAD=y", cores3_defaults)
        self.assertIn("CONFIG_SPIRAM_MODE_OCT=y", watch_defaults)
        self.assertIn("CONFIG_SPIRAM_USE_MALLOC=y", defaults)
        self.assertIn("CONFIG_SPIRAM_MEMTEST=y", defaults)
        self.assertIn(
            "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768",
            defaults,
        )

        source = (
            ROOT / "firmware" / "main" / "src" / "main.cpp"
        ).read_text()
        self.assertIn(
            "kRuntimeThreadStackBytes = 16 * 1024",
            source,
        )


if __name__ == "__main__":
    unittest.main()
