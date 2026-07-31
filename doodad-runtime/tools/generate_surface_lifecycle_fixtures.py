#!/usr/bin/env python3
"""Generate stale/recovered cross-surface revisions for every suite package."""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

from doodad_cli.conformance import validate_scenario, validate_surface_state


ROOT = Path(__file__).resolve().parents[1]


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def revision(
    baseline: dict[str, Any],
    number: int,
    observed_at_ms: int,
    freshness: str,
) -> dict[str, Any]:
    snapshot = copy.deepcopy(baseline)
    snapshot["domain_revision"] = number
    snapshot["observed_at_ms"] = observed_at_ms
    snapshot["freshness"] = freshness
    for projection in snapshot["surfaces"].values():
        projection["revision"] = number
    app = snapshot["surfaces"]["app"]
    app["state"] = {
        "provider": "mock",
        "interactive": True,
        "freshness": freshness,
    }
    if "glance" in snapshot["surfaces"]:
        snapshot["surfaces"]["glance"]["secondary"] = (
            "Cached · 18 min" if freshness == "stale" else "Updated now"
        )
    return snapshot


def scenario(
    slug: str,
    app_id: str,
    baseline: dict[str, Any],
    stale: dict[str, Any],
    recovered: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "id": f"{slug}.surface.lifecycle",
        "app_id": app_id,
        "initial_state": {
            "wall_time_ms": 1700000000000,
            "timezone_offset_minutes": -420,
            "app_state": "foreground",
            "display_state": "awake",
            "connectivity": "online",
        },
        "steps": [
            {"op": "surface.publish", "snapshot": baseline},
            {"op": "clock.advance", "milliseconds": 1000},
            {
                "op": "provider.emit",
                "provider": "mock",
                "event": f"{slug}.stale",
                "revision": 1,
                "status": "stale",
                "payload": {"age_minutes": 18},
            },
            {"op": "surface.publish", "snapshot": stale},
            {
                "op": "lifecycle.set",
                "connectivity": "offline",
            },
            {"op": "clock.advance", "milliseconds": 1000},
            {
                "op": "lifecycle.set",
                "connectivity": "online",
            },
            {
                "op": "provider.emit",
                "provider": "mock",
                "event": f"{slug}.recovered",
                "revision": 2,
                "status": "current",
                "payload": {"reconciled": True},
            },
            {"op": "surface.publish", "snapshot": recovered},
            {
                "op": "action.dispatch",
                "target": f"{slug}.primary",
                "value": {"source": "voice"},
            },
            {
                "op": "assert.state",
                "equals": {
                    f"surfaces.{app_id}.domain_revision": 3,
                    f"surfaces.{app_id}.freshness": "current",
                    "providers.mock.revision": 2,
                    "lifecycle.connectivity": "online",
                    "actions.count": 1,
                },
            },
        ],
    }


def main() -> None:
    catalog = json.loads(
        (ROOT / "apps" / "conformance-suite.json").read_text()
    )
    for app in catalog["apps"]:
        directory = ROOT / "apps" / app["slug"]
        baseline = json.loads(
            (directory / "surfaces" / "baseline.surface.json").read_text()
        )
        stale = revision(baseline, 2, 1000, "stale")
        recovered = revision(baseline, 3, 2000, "current")
        validate_surface_state(stale)
        validate_surface_state(recovered)
        flow = scenario(
            app["slug"], app["id"], baseline, stale, recovered
        )
        validate_scenario(flow)
        write_json(
            directory / "surfaces" / "stale.surface.json", stale
        )
        write_json(
            directory / "surfaces" / "recovered.surface.json", recovered
        )
        write_json(
            directory / "scenarios" / "surface-lifecycle.scenario.json",
            flow,
        )


if __name__ == "__main__":
    main()
