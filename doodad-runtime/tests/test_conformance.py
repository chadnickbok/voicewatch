from __future__ import annotations

import copy
import unittest
from pathlib import Path

from tools.doodad_cli.conformance import (
    ScenarioRunner,
    validate_scenario,
    validate_surface_state,
)
from tools.doodad_cli.contract import DoodadError, read_json


ROOT = Path(__file__).resolve().parents[1]


class SurfaceStateContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.surface = read_json(
            ROOT / "fixtures" / "surfaces" / "timer-running.surface.json"
        )

    def test_timer_surface_covers_every_declared_projection(self) -> None:
        validate_surface_state(self.surface)

    def test_projection_revision_must_match_domain_revision(self) -> None:
        document = copy.deepcopy(self.surface)
        document["surfaces"]["complication"]["revision"] = 1
        with self.assertRaisesRegex(DoodadError, "does not match domain_revision"):
            validate_surface_state(document)

    def test_declared_surface_cannot_be_silently_omitted(self) -> None:
        document = copy.deepcopy(self.surface)
        del document["surfaces"]["voice"]
        with self.assertRaisesRegex(DoodadError, "exactly cover"):
            validate_surface_state(document)

    def test_active_notification_requires_content_and_privacy(self) -> None:
        document = copy.deepcopy(self.surface)
        notification = document["surfaces"]["notification"]
        notification["status"] = "active"
        with self.assertRaisesRegex(DoodadError, "missing 'title'"):
            validate_surface_state(document)


class DeterministicScenarioTests(unittest.TestCase):
    def setUp(self) -> None:
        self.scenario = read_json(
            ROOT / "fixtures" / "scenarios" / "timer-reboot.scenario.json"
        )

    def test_timer_fires_at_deadline_across_reboot(self) -> None:
        validate_scenario(self.scenario)
        result = ScenarioRunner().run(self.scenario)
        self.assertEqual(result.steps_executed, 13)
        self.assertEqual(result.state["clock"]["scenario_ms"], 60_000)
        self.assertEqual(result.state["clock"]["uptime_ms"], 2_500)
        self.assertEqual(result.state["clock"]["boot_generation"], 2)
        self.assertEqual(
            result.state["providers"]["scheduler"]["event"], "timer.fired"
        )
        surfaces = result.state["surfaces"]["dev.doodad.timer"]["surfaces"]
        self.assertEqual(surfaces["notification"]["status"], "active")
        self.assertEqual(surfaces["ongoing"]["status"], "inactive")

    def test_wall_clock_change_does_not_change_monotonic_clocks(self) -> None:
        scenario = {
            "schema_version": 1,
            "id": "clock.wall.change",
            "app_id": "dev.doodad.clocktest",
            "initial_state": {
                "wall_time_ms": 1000,
                "timezone_offset_minutes": 0,
                "app_state": "foreground",
                "display_state": "awake",
                "connectivity": "online",
            },
            "steps": [
                {"op": "clock.advance", "milliseconds": 250},
                {"op": "clock.set_wall", "wall_time_ms": -5000},
                {
                    "op": "assert.state",
                    "equals": {
                        "clock.scenario_ms": 250,
                        "clock.uptime_ms": 250,
                        "clock.wall_time_ms": -5000,
                    },
                },
            ],
        }
        result = ScenarioRunner().run(scenario)
        self.assertEqual(result.state["clock"]["scenario_ms"], 250)

    def test_impossible_lifecycle_transition_fails(self) -> None:
        scenario = copy.deepcopy(self.scenario)
        scenario["initial_state"]["app_state"] = "crashed"
        scenario["steps"] = [
            {"op": "lifecycle.set", "app_state": "foreground"}
        ]
        with self.assertRaisesRegex(DoodadError, "cannot transition"):
            ScenarioRunner().run(scenario)

    def test_provider_revision_must_increase(self) -> None:
        scenario = copy.deepcopy(self.scenario)
        provider = {
            "op": "provider.emit",
            "provider": "weather",
            "event": "weather.updated",
            "revision": 4,
            "status": "current",
            "payload": {},
        }
        scenario["steps"] = [provider, copy.deepcopy(provider)]
        with self.assertRaisesRegex(DoodadError, "revision must increase"):
            ScenarioRunner().run(scenario)

    def test_surface_timestamp_must_match_scenario_clock(self) -> None:
        scenario = copy.deepcopy(self.scenario)
        scenario["steps"] = [scenario["steps"][4]]
        with self.assertRaisesRegex(DoodadError, "observed_at_ms"):
            ScenarioRunner().run(scenario)


if __name__ == "__main__":
    unittest.main()
