from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class WeatherAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(ROOT, ROOT / "apps" / "weather")

    def setUp(self) -> None:
        self.native = NativeHost(ROOT)
        self.native.start_wasm(self.package.wasm)

    def tearDown(self) -> None:
        self.native.close()

    def deliver_fixture(self, name: str) -> None:
        fixture_root = ROOT / "reference" / "weather-fixtures" / "v2"
        source = json.loads(
            (fixture_root / "sources" / f"{name}.json").read_text()
        )
        self.native.deliver_weather_payload(
            (fixture_root / "generated" / f"{name}.cbor").read_bytes(),
            freshness=source["freshness"],
        )

    def snapshot_nodes(self) -> tuple[dict, dict[str, dict]]:
        snapshot = json.loads(self.native.scene_snapshot())
        return snapshot, {node["id"]: node for node in snapshot["nodes"]}

    def primary_text(self, nodes: dict[str, dict], node_id: str) -> str:
        return nodes[node_id]["props"]["primary_text"]

    def test_v2_snapshot_and_hourly_navigation(self) -> None:
        self.assertEqual(self.native.node_text("weather.summary"), "62°")
        self.assertEqual(self.native.node_text("weather.status"), "Now")
        self.assertEqual(self.native.provider_request_count(), 1)

        self.native.deliver_provider()
        self.assertEqual(self.native.node_text("weather.summary"), "62°")
        self.assertEqual(self.native.node_text("weather.symbol"), "Partly cloudy")
        self.assertEqual(self.native.node_text("weather.status"), "12m")

        self.native.dispatch_semantic_action(
            "weather.primary",
            "weather.hourly",
            "tap",
        )
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.hourly-screen")
        self.assertEqual(self.primary_text(nodes, "weather.hour-now-temp"), "62°")
        self.assertEqual(self.primary_text(nodes, "weather.hour-10-temp"), "63°")
        self.assertEqual(self.primary_text(nodes, "weather.hour-11-temp"), "65°")
        self.assertEqual(self.primary_text(nodes, "weather.hour-12-temp"), "66°")
        self.assertEqual(self.primary_text(nodes, "weather.hour-now-label"), "NOW")
        self.assertEqual(self.primary_text(nodes, "weather.hour-10-label"), "11")
        self.assertEqual(self.primary_text(nodes, "weather.hour-11-label"), "12")
        self.assertEqual(self.primary_text(nodes, "weather.hour-12-label"), "1")
        self.assertEqual(nodes["weather.hourly-tiles"]["semantics"]["label"], "Hourly forecast")
        self.assertEqual(nodes["weather.rain-chart"]["props"]["samples"], [0, 0, 0, 0])

        self.native.dispatch_semantic_action(
            "weather.daily-action",
            "weather.daily",
            "tap",
        )
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.daily-screen")
        self.assertEqual(self.primary_text(nodes, "weather.daily-location"), "San Francisco")
        self.assertEqual(self.primary_text(nodes, "weather.day-today-low"), "54°")
        self.assertEqual(self.primary_text(nodes, "weather.day-today-high"), "67°")
        self.assertEqual(self.primary_text(nodes, "weather.day-mon-label"), "SUN")
        self.assertEqual(self.primary_text(nodes, "weather.day-tue-label"), "MON")
        self.assertEqual(self.primary_text(nodes, "weather.day-wed-label"), "TUE")

        self.native.dispatch_semantic_action(
            "weather.details-action",
            "weather.details",
            "tap",
        )
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.details-screen")
        self.assertEqual(self.primary_text(nodes, "weather.humidity-value"), "49%")
        self.assertEqual(self.primary_text(nodes, "weather.wind-value"), "8")
        self.assertEqual(self.primary_text(nodes, "weather.wind-unit"), "mph")
        self.assertEqual(self.primary_text(nodes, "weather.uv-value"), "3")
        self.assertEqual(self.primary_text(nodes, "weather.uv-unit"), "Moderate")
        self.assertEqual(self.primary_text(nodes, "weather.sunrise-value"), "6:12")

        self.native.dispatch_semantic_action(
            "weather.rain-preview-action",
            "weather.rain-preview",
            "tap",
        )
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.rain-screen")
        # The baseline fixture has no imminent rain, so this explicit preview keeps
        # the deterministic design-oracle content instead of fabricating live data.
        self.assertEqual(self.primary_text(nodes, "weather.rain-title"), "Rain in\n20 min")
        self.assertEqual(self.primary_text(nodes, "weather.rain-duration"), "Light rain for 35 min")

        # Retrying from the preview must leave a valid mounted scene while the
        # provider request is pending. The next native-host cycle is offline.
        self.native.dispatch_semantic_action(
            "weather.rain-status",
            "weather.retry",
            "tap",
        )
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.rain-screen")
        self.assertEqual(self.primary_text(nodes, "weather.rain-title"), "Rain in\n20 min")
        self.native.deliver_provider()
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.home")
        self.assertEqual(self.primary_text(nodes, "weather.status"), "Offline")

    def test_extreme_fixture_updates_values_and_condition_icon(self) -> None:
        self.deliver_fixture("extreme")
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.home")
        self.assertEqual(self.primary_text(nodes, "weather.location"), "Palm Springs")
        self.assertEqual(self.primary_text(nodes, "weather.summary"), "108°")
        self.assertEqual(self.primary_text(nodes, "weather.symbol"), "Hot")
        self.assertEqual(nodes["weather.condition-icon"]["props"]["icon"], "condition_hot")
        self.assertEqual(self.primary_text(nodes, "weather.high"), "H 112°")
        self.assertEqual(self.primary_text(nodes, "weather.low"), "L 82°")
        self.assertEqual(nodes["weather.summary"]["semantics"]["value"], "108 degrees")
        self.assertEqual(nodes["weather.symbol"]["semantics"]["value"], "Hot")
        self.assertEqual(nodes["weather.condition-icon"]["semantics"]["label"], "Hot")

        self.native.dispatch_semantic_action(
            "weather.primary",
            "weather.hourly",
            "tap",
        )
        _, nodes = self.snapshot_nodes()
        self.assertEqual(nodes["weather.hourly-condition-icon"]["semantics"]["label"], "Hot")
        self.assertEqual(nodes["weather.hour-now-icon"]["semantics"]["label"], "Hot")

        self.native.dispatch_semantic_action(
            "weather.daily-action",
            "weather.daily",
            "tap",
        )
        _, nodes = self.snapshot_nodes()
        self.assertEqual(nodes["weather.day-today-icon"]["semantics"]["label"], "Hot")

        self.native.dispatch_semantic_action(
            "weather.details-action",
            "weather.details",
            "tap",
        )
        _, nodes = self.snapshot_nodes()
        self.assertEqual(nodes["weather.details-temperature"]["semantics"]["value"], "108 degrees")
        self.assertEqual(nodes["weather.details-condition-icon"]["semantics"]["label"], "Hot")
        self.assertEqual(nodes["weather.humidity-value"]["semantics"]["label"], "12 percent")
        self.assertEqual(nodes["weather.wind-value"]["semantics"]["label"], "6 miles per hour")
        self.assertEqual(nodes["weather.uv-value"]["semantics"]["label"], "UV index 11, Extreme")
        self.assertEqual(nodes["weather.sunrise-value"]["semantics"]["label"], "Sunrise at 5:47")

    def test_rain_fixture_routes_and_updates_minutely_chart(self) -> None:
        self.deliver_fixture("rain")
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.rain-screen")
        self.assertEqual(self.primary_text(nodes, "weather.rain-title"), "Rain in\n20 min")
        self.assertEqual(self.primary_text(nodes, "weather.rain-duration"), "Light rain for 35 min")
        self.assertEqual(self.primary_text(nodes, "weather.rain-probability-value"), "32%")
        self.assertEqual(nodes["weather.rain-title"]["semantics"]["label"], "Rain in 20 minutes")
        self.assertEqual(nodes["weather.rain-probability-value"]["semantics"]["label"], "32 percent")
        self.assertEqual(
            nodes["weather.rain-bars"]["props"]["samples"],
            [4, 8, 15, 24, 35, 48, 62, 72, 68, 55, 39, 22, 9],
        )

    def test_root_page_changed_action_navigates_forward_and_back(self) -> None:
        self.native.deliver_provider()
        route = "weather.home"
        for expected in (
            "weather.hourly-screen",
            "weather.daily-screen",
            "weather.details-screen",
        ):
            self.native.dispatch_semantic_action(
                route,
                "weather.page-changed",
                "page_changed",
                1,
            )
            snapshot, _ = self.snapshot_nodes()
            self.assertEqual(snapshot["screen_id"], expected)
            route = expected

        # Swiping past either end is intentionally a no-op.
        self.native.dispatch_semantic_action(
            route, "weather.page-changed", "page_changed", 1,
        )
        snapshot, _ = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.details-screen")

        for expected in (
            "weather.daily-screen",
            "weather.hourly-screen",
            "weather.home",
        ):
            self.native.dispatch_semantic_action(
                route,
                "weather.page-changed",
                "page_changed",
                -1,
            )
            snapshot, _ = self.snapshot_nodes()
            self.assertEqual(snapshot["screen_id"], expected)
            route = expected

    def test_rain_route_participates_in_swipe_navigation(self) -> None:
        self.deliver_fixture("rain")
        self.native.dispatch_semantic_action(
            "weather.rain-screen",
            "weather.page-changed",
            "page_changed",
            -1,
        )
        snapshot, _ = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.home")

        self.native.dispatch_semantic_action(
            "weather.home",
            "weather.page-changed",
            "page_changed",
            1,
        )
        snapshot, _ = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.rain-screen")

        self.native.dispatch_semantic_action(
            "weather.rain-screen",
            "weather.page-changed",
            "page_changed",
            1,
        )
        snapshot, _ = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.daily-screen")

    def test_stale_fixture_communicates_age(self) -> None:
        self.deliver_fixture("stale")
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.home")
        self.assertEqual(self.primary_text(nodes, "weather.status"), "35m")
        self.assertEqual(nodes["weather.status"]["semantics"]["label"], "Weather updated 35 minutes ago")

    def test_error_fixture_promotes_retry_status(self) -> None:
        self.deliver_fixture("error")
        snapshot, nodes = self.snapshot_nodes()
        self.assertEqual(snapshot["screen_id"], "weather.home")
        self.assertEqual(self.primary_text(nodes, "weather.status"), "Retry")
        self.assertEqual(nodes["weather.status"]["semantics"]["label"], "Weather unavailable, retry")


if __name__ == "__main__":
    unittest.main()
