#!/usr/bin/env python3
"""Capture Weather's five decisive scenes at the 1.3x text scale."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.parallax_pipeline import run_perfect_render_suite


REFERENCE = ROOT / "reference"
SOURCE_SUITE = REFERENCE / "perfect-render-suite.json"
SUITE = REFERENCE / "weather-large-font-render-suite.json"
OUTPUT = REFERENCE / "weather-foundations" / "generated" / "phase5-large-font"


def generate(*, capture: bool) -> None:
    source = json.loads(SOURCE_SUITE.read_text(encoding="utf-8"))
    entries = []
    for source_entry in source["entries"]:
        if source_entry["app_slug"] != "weather":
            continue
        entry = copy.deepcopy(source_entry)
        entry["capture_phase"] = "large_font"
        entry["font_scale_milli"] = 1300
        entry["review"] = {
            "status": "pending",
            "reviewer": "Project Nimbus large-font pass",
            "reviewed_at": "2026-08-01",
            "notes": (
                "The production Weather snapshot rendered at font scale 1.3 "
                "through both Wear Compose and LVGL."
            ),
        }
        entries.append(entry)

    if len(entries) != 5:
        raise RuntimeError(f"expected five Weather oracle entries, got {len(entries)}")
    suite = {
        "schema_version": 1,
        "id": "weather-large-font-goldens",
        "entries": entries,
    }
    SUITE.write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {SUITE.relative_to(ROOT)}")

    if capture:
        run = run_perfect_render_suite(ROOT, SUITE, OUTPUT, app_slug="weather")
        print(f"Captured {run.case_count} large-font pairs")
        print(f"Contact sheet: {run.contact_sheet}")
        print(f"Report: {run.report.html}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-capture", action="store_true")
    options = parser.parse_args()
    generate(capture=not options.skip_capture)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
