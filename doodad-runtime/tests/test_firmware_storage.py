from __future__ import annotations

import csv
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirmwareStorageTests(unittest.TestCase):
    def test_package_filesystem_supports_generation_long_names(self) -> None:
        common = (ROOT / "firmware" / "sdkconfig.defaults").read_text()
        build_script = (ROOT / "scripts" / "build-firmware.sh").read_text()
        for board in ("cores3", "t-watch-s3"):
            board_defaults = (
                ROOT / "firmware" / "boards" / board / "sdkconfig.defaults"
            ).read_text()
            combined = common + board_defaults
            self.assertIn("CONFIG_FATFS_LFN_HEAP=y", combined)
            self.assertIn("CONFIG_FATFS_MAX_LFN=255", combined)
            self.assertNotIn("CONFIG_FATFS_LFN_NONE=y", combined)

        self.assertIn("migrate_fatfs_lfn", build_script)
        self.assertIn("'CONFIG_FATFS_LFN_HEAP=y'", build_script)
        self.assertIn("'CONFIG_FATFS_MAX_LFN=255'", build_script)

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

    def test_legacy_onboard_fallback_precedes_sd_and_recovery(self) -> None:
        source = (ROOT / "firmware" / "main" / "src" / "main.cpp").read_text()
        onboard = source.index("load_onboard_app")
        sd = source.index("load_microsd_app")
        # Helper functions can also reference the recovery image; compare the
        # actual boot-selection call that follows onboard and microSD.
        embedded = source.index("running = run_app(embedded_app_image())", sd)
        self.assertLess(onboard, sd)
        self.assertLess(sd, embedded)

        defaults = (
            ROOT / "firmware" / "sdkconfig.defaults"
        ).read_text()
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", defaults)

        package_source = (
            ROOT / "firmware" / "main" / "src" / "app_sources.cpp"
        ).read_text()
        self.assertNotIn("format_if_mount_failed = true", package_source)

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

    def test_personal_install_and_live_switch_loop_is_wired(self) -> None:
        main = (ROOT / "firmware" / "main" / "src" / "main.cpp").read_text()
        voice = (
            ROOT / "firmware" / "main" / "src" / "voice_service.cpp"
        ).read_text()
        display = (
            ROOT / "firmware" / "main" / "src" / "display.cpp"
        ).read_text()
        runner = (
            ROOT / "firmware" / "main" / "src" / "app_runner.cpp"
        ).read_text()
        packages = (
            ROOT / "firmware" / "main" / "src" / "package_service.cpp"
        ).read_text()

        self.assertIn('"app.ready"', voice)
        self.assertIn("package_service_offer", voice)
        self.assertIn("esp_http_client_read", packages)
        self.assertIn("format_if_mount_failed = false", packages)
        self.assertIn("package_service_poll_launch", main)
        self.assertLess(
            main.index("package_service_init"),
            main.index("app_runtime_init"),
        )
        self.assertIn("restore_previous_package", main)
        self.assertIn("run_installed_package(previous", main)
        self.assertIn("package_service_recover_current", main)
        self.assertIn("safe current is already resident", main)
        self.assertIn("display_publish_app_current_recovery", main)
        self.assertIn("installed_launcher_now", display)
        self.assertIn('"Launch now"', display)
        self.assertIn("g_ready_deferred_by_overlay", display)
        ready_handler = display[
            display.index("void package_ready_event_now") :
            display.index("void app_running_event_now")
        ]
        self.assertNotIn("dismiss_overlay", ready_handler)
        running_handler = display[
            display.index("void app_running_event_now") :
            display.index("void app_rollback_event_now")
        ]
        self.assertLess(
            running_handler.index('voice_service_request("system.voice.cancel"'),
            running_handler.index("dismiss_overlay"),
        )
        self.assertIn("xSemaphoreTake(completion.semaphore", display)
        self.assertIn("open_crash_recovery", display)
        self.assertNotIn("esp_restart", main)

        # A WireDocument allocation address is not a generation identity:
        # allocators may reuse it after a hot switch. Immediate callbacks use
        # the document pointer only as an origin check; queued delivery is
        # authorized by a never-reused mount epoch, and display invalidates
        # that pair before freeing any active or pending document.
        self.assertIn("std::uint64_t ui_owner_epoch", runner)
        self.assertIn("g_ui_owner_epoch_sequence", runner)
        self.assertNotIn("std::uintptr_t ui_owner_token", runner)
        post_event = runner[
            runner.index("bool post_ui_event") :
            runner.index("void app_runtime_update")
        ]
        self.assertLess(
            post_event.index("snapshot_ui_owner"),
            post_event.index("event_is_valid"),
        )
        self.assertIn("ui_owner_still_active", post_event)
        ordinary_post = runner[
            runner.index("bool app_post_ui_event") :
            runner.index("bool app_post_embedded_ui_event")
        ]
        self.assertIn("post_ui_event(event, false)", ordinary_post)
        self.assertIn("app_runtime_invalidate_ui_mount", display)
        self.assertNotIn("delete g_active_document", display)
        self.assertNotIn("delete g_pending_document", display)


if __name__ == "__main__":
    unittest.main()
