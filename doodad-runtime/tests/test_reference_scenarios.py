from __future__ import annotations

import copy
import unittest
from pathlib import Path

from tools.doodad_cli.contract import DoodadError
from tools.doodad_cli.reference_scenario import (
    flatten_semantics,
    load_reference_scenarios,
    validate_reference_scenario,
)


ROOT = Path(__file__).resolve().parents[1]
KNOWN_PROFILES = {
    "wear_round_small",
    "wear_round_large",
    "watch_square_240",
}


class ReferenceScenarioContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.scenarios = load_reference_scenarios(ROOT)
        self.timer = next(
            scenario
            for scenario in self.scenarios
            if scenario["scene"] == "timer-running"
        )

    def test_initial_oracle_catalog_has_ten_unique_scenes(self) -> None:
        self.assertEqual(len(self.scenarios), 10)
        self.assertEqual(
            len({scenario["scene"] for scenario in self.scenarios}),
            10,
        )

    def test_every_scene_covers_round_and_square_geometry(self) -> None:
        for scenario in self.scenarios:
            with self.subTest(scenario=scenario["id"]):
                self.assertIn("wear_round_small", scenario["render_profiles"])
                self.assertIn("wear_round_large", scenario["render_profiles"])
                self.assertIn("watch_square_240", scenario["render_profiles"])

    def test_expected_semantic_ids_are_unique(self) -> None:
        for scenario in self.scenarios:
            with self.subTest(scenario=scenario["id"]):
                ids = [
                    node["id"]
                    for node in flatten_semantics(
                        scenario["expected_semantics"]
                    )
                ]
                self.assertEqual(len(ids), len(set(ids)))

    def test_unknown_profile_is_rejected(self) -> None:
        scenario = copy.deepcopy(self.timer)
        scenario["render_profiles"].append("imaginary_watch")
        with self.assertRaisesRegex(DoodadError, "unknown profiles"):
            validate_reference_scenario(
                scenario,
                project_root=ROOT,
                known_profiles=KNOWN_PROFILES,
            )

    def test_out_of_range_animation_fraction_is_rejected(self) -> None:
        scenario = copy.deepcopy(self.timer)
        scenario["interaction"]["animation_fraction"] = 1.01
        with self.assertRaisesRegex(DoodadError, "animation_fraction"):
            validate_reference_scenario(
                scenario,
                project_root=ROOT,
                known_profiles=KNOWN_PROFILES,
            )

    def test_semantic_ids_cannot_repeat(self) -> None:
        scenario = copy.deepcopy(self.timer)
        scenario["expected_semantics"]["children"][1]["id"] = (
            scenario["expected_semantics"]["children"][0]["id"]
        )
        with self.assertRaisesRegex(DoodadError, "duplicates semantic id"):
            validate_reference_scenario(
                scenario,
                project_root=ROOT,
                known_profiles=KNOWN_PROFILES,
            )

    def test_repository_links_are_checked(self) -> None:
        scenario = copy.deepcopy(self.timer)
        scenario["appspec"] = "apps/timer/missing.json"
        with self.assertRaisesRegex(DoodadError, "does not exist"):
            validate_reference_scenario(
                scenario,
                project_root=ROOT,
                known_profiles=KNOWN_PROFILES,
            )


if __name__ == "__main__":
    unittest.main()
