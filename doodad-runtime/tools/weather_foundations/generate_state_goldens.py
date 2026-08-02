#!/usr/bin/env python3
"""Record checked Weather v2 state traces and their dual-renderer goldens."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.parallax_pipeline import run_perfect_render_suite
from doodad_cli.scene_trace import record_flow_trace, verify_trace_bundle_fresh


REFERENCE = ROOT / "reference"
SUITE = REFERENCE / "weather-state-render-suite.json"
OUTPUT = REFERENCE / "weather-foundations" / "generated" / "phase5-states"
STATES = ("baseline", "extreme", "rain", "stale", "error")


def suite_entry(
    name: str,
    sequence: int,
    snapshot_sha256: str,
) -> dict[str, object]:
    return {
        "app_slug": "weather",
        "trace": f"traces/weather/states/{name}/trace.json",
        "sequence": sequence,
        "snapshot_sha256": snapshot_sha256,
        "capture_phase": f"{name}_state",
        "profile_id": "watch_square_240",
        "compose": {"mode": "host", "version": "wear-compose-1.6.2"},
        "lvgl": {"mode": "simulator", "version": "9.5.0"},
        "comparison_policy": "square-app-baseline",
        "review": {
            "status": "pending",
            "reviewer": "Project Nimbus Phase 5",
            "reviewed_at": "2026-08-01",
            "notes": (
                f"Provider-v2 {name} fixture rendered through the real "
                "Weather Wasm guest and shared semantic snapshot."
            ),
        },
    }


def generate(*, capture: bool) -> None:
    entries: list[dict[str, object]] = []
    for name in STATES:
        directory = REFERENCE / "traces" / "weather" / "states" / name
        bundle = record_flow_trace(
            ROOT,
            "weather",
            [{"kind": "deliver_weather_fixture", "fixture": name}],
            directory,
            scenario_id=f"state-{name}",
        )
        proof = verify_trace_bundle_fresh(ROOT, directory)
        if not proof["passed"] or proof["wasm_calls"] != 0:
            raise RuntimeError(f"Weather {name} trace did not replay cleanly")
        final = bundle.trace["entries"][-1]
        entries.append(
            suite_entry(
                name,
                int(final["sequence"]),
                str(final["after_snapshot_sha256"]),
            )
        )
        print(
            f"weather state: {name}: sequence {final['sequence']} "
            f"{final['screen_id']}"
        )

    suite = {
        "schema_version": 1,
        "id": "weather-state-goldens",
        "entries": entries,
    }
    SUITE.write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {SUITE.relative_to(ROOT)}")

    if capture:
        run = run_perfect_render_suite(ROOT, SUITE, OUTPUT, app_slug="weather")
        print(f"Captured {run.case_count} state pairs")
        print(f"Contact sheet: {run.contact_sheet}")
        print(f"Report: {run.report.html}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--skip-capture",
        action="store_true",
        help="record deterministic traces and suite without running Compose",
    )
    options = parser.parse_args()
    generate(capture=not options.skip_capture)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
