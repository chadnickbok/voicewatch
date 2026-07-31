from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.appspec import validate_appspec
from tools.doodad_cli.appspec_cbor import compile_canonical_cbor
from tools.doodad_cli.conformance import ScenarioRunner, validate_surface_state
from tools.doodad_cli.contract import load_abi, read_json, validate_manifest


ROOT = Path(__file__).resolve().parents[1]

DOMAIN_CAPABILITIES = {
    "calendar": "calendar.sync",
    "voice-notes": "audio.capture",
    "medication": "medication.schedule",
    "sensor-recorder": "sensor.record",
    "sleep": "sleep.track",
    "media": "media.remote",
    "navigation": "navigation.route",
    "transit": "transit.read",
    "smart-home": "home.control",
    "sports": "sports.read",
    "wallet": "wallet.read",
    "remote-control": "remote.control",
    "workout": "workout.store",
    "snake": "game.clock",
}


class TwentyAppSuiteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.catalog = read_json(ROOT / "apps" / "conformance-suite.json")
        cls.abi = load_abi(ROOT)

    def test_catalog_contains_exactly_twenty_separate_packages(self) -> None:
        apps = self.catalog["apps"]
        self.assertEqual(len(apps), 20)
        self.assertEqual([app["index"] for app in apps], list(range(1, 21)))
        self.assertEqual(len({app["slug"] for app in apps}), 20)
        self.assertEqual(len({app["id"] for app in apps}), 20)
        self.assertEqual(apps[-1]["slug"], "snake")

    def test_every_package_has_build_and_contract_artifacts(self) -> None:
        for app in self.catalog["apps"]:
            with self.subTest(app=app["slug"]):
                directory = ROOT / "apps" / app["slug"]
                for relative in (
                    "Cargo.toml",
                    "src/lib.rs",
                    "manifest.json",
                    "package.json",
                    "appspec.json",
                    "appspec.cbor",
                    "interaction.cbor",
                    "surfaces/baseline.surface.json",
                    "surfaces/stale.surface.json",
                    "surfaces/recovered.surface.json",
                    "scenarios/baseline.scenario.json",
                    "scenarios/surface-lifecycle.scenario.json",
                ):
                    self.assertTrue((directory / relative).is_file(), relative)

                manifest_path = directory / "manifest.json"
                manifest = read_json(manifest_path)
                validate_manifest(manifest, self.abi, manifest_path)
                self.assertEqual(manifest["id"], app["id"])

                package = read_json(directory / "package.json")
                self.assertEqual(package["suite_index"], app["index"])
                self.assertEqual(package["mode"], app["mode"])
                self.assertEqual(package["surfaces"], app["surfaces"])

    def test_mocked_domains_use_distinct_capability_imports(self) -> None:
        import_names = set()
        for slug, capability in DOMAIN_CAPABILITIES.items():
            with self.subTest(app=slug):
                manifest = read_json(
                    ROOT / "apps" / slug / "manifest.json"
                )
                self.assertIn(capability, manifest["capabilities"])
                self.assertNotIn(
                    "fixture.interact", manifest["capabilities"]
                )
                definition = self.abi["capabilities"][capability]
                self.assertTrue(definition["mocked"])
                import_names.add(definition["import_name"])
        self.assertEqual(len(import_names), len(DOMAIN_CAPABILITIES))

    def test_every_appspec_matches_its_canonical_device_bytes(self) -> None:
        for app in self.catalog["apps"]:
            with self.subTest(app=app["slug"]):
                directory = ROOT / "apps" / app["slug"]
                document = read_json(directory / "appspec.json")
                stats = validate_appspec(document)
                self.assertGreaterEqual(stats.nodes, 2)
                self.assertEqual(
                    compile_canonical_cbor(document),
                    (directory / "appspec.cbor").read_bytes(),
                )

    def test_every_declared_surface_is_present_at_one_revision(self) -> None:
        for app in self.catalog["apps"]:
            with self.subTest(app=app["slug"]):
                surface = read_json(
                    ROOT
                    / "apps"
                    / app["slug"]
                    / "surfaces"
                    / "baseline.surface.json"
                )
                validate_surface_state(surface)
                self.assertEqual(surface["app_id"], app["id"])
                self.assertEqual(surface["declared_surfaces"], app["surfaces"])
                self.assertEqual(
                    {projection["revision"] for projection in surface["surfaces"].values()},
                    {surface["domain_revision"]},
                )

    def test_every_baseline_scenario_is_deterministic_and_interactive(self) -> None:
        runner = ScenarioRunner()
        for app in self.catalog["apps"]:
            with self.subTest(app=app["slug"]):
                scenario = read_json(
                    ROOT
                    / "apps"
                    / app["slug"]
                    / "scenarios"
                    / "baseline.scenario.json"
                )
                first = runner.run(scenario)
                second = runner.run(json.loads(json.dumps(scenario)))
                self.assertEqual(first.state, second.state)
                self.assertEqual(first.trace, second.trace)
                self.assertEqual(first.state["actions"]["count"], 1)
                self.assertEqual(
                    first.state["surfaces"][app["id"]]["declared_surfaces"],
                    app["surfaces"],
                )

    def test_every_surface_recovers_at_one_atomic_revision(self) -> None:
        runner = ScenarioRunner()
        for app in self.catalog["apps"]:
            with self.subTest(app=app["slug"]):
                directory = ROOT / "apps" / app["slug"]
                for filename, expected_revision, freshness in (
                    ("stale.surface.json", 2, "stale"),
                    ("recovered.surface.json", 3, "current"),
                ):
                    snapshot = read_json(
                        directory / "surfaces" / filename
                    )
                    validate_surface_state(snapshot)
                    self.assertEqual(
                        {
                            projection["revision"]
                            for projection in snapshot["surfaces"].values()
                        },
                        {expected_revision},
                    )
                    self.assertEqual(snapshot["freshness"], freshness)

                scenario = read_json(
                    directory
                    / "scenarios"
                    / "surface-lifecycle.scenario.json"
                )
                result = runner.run(scenario)
                snapshot = result.state["surfaces"][app["id"]]
                self.assertEqual(snapshot["domain_revision"], 3)
                self.assertEqual(snapshot["freshness"], "current")


if __name__ == "__main__":
    unittest.main()
