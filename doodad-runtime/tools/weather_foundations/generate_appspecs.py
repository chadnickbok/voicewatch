#!/usr/bin/env python3
"""Split Nimbus' visual master into bounded, mountable Weather AppSpecs."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.appspec_cbor import compile_canonical_cbor


MASTER = ROOT / "reference" / "weather-foundations" / "weather-app-master.json"
APP_DIRECTORY = ROOT / "apps" / "weather"
SCREENS = APP_DIRECTORY / "screens"
PAGE_OUTPUTS = {
    "weather.current": (APP_DIRECTORY / "appspec.json", APP_DIRECTORY / "appspec.cbor", "weather.home"),
    "weather.hourly": (SCREENS / "hourly.json", SCREENS / "hourly.cbor", "weather.hourly-screen"),
    "weather.daily-page": (SCREENS / "daily.json", SCREENS / "daily.cbor", "weather.daily-screen"),
    "weather.details-page": (SCREENS / "details.json", SCREENS / "details.cbor", "weather.details-screen"),
    "weather.rain-page": (SCREENS / "rain.json", SCREENS / "rain.cbor", "weather.rain-screen"),
}
PAGE_LABELS = {
    "weather.current": "Current weather",
    "weather.hourly": "Hourly forecast",
    "weather.daily-page": "Daily forecast",
    "weather.details-page": "Weather details",
    "weather.rain-page": "Imminent rain",
}


def documents() -> list[tuple[Path, Path, dict[str, object]]]:
    master = json.loads(MASTER.read_text(encoding="utf-8"))
    pager = master["screen"]["props"]["children"][0]
    if pager["id"] != "weather.pages" or pager["type"] != "pager":
        raise ValueError("Weather master must contain one weather.pages pager")
    pages = {page["id"]: page for page in pager["props"]["children"]}
    if set(pages) != set(PAGE_OUTPUTS):
        raise ValueError("Weather master page set differs from the five mount targets")
    result = []
    for page_id, (json_path, cbor_path, screen_id) in PAGE_OUTPUTS.items():
        document = {
            "schema_version": 1,
            "app_id": "weather",
            "screen": {
                "id": screen_id,
                "type": "screen",
                "props": {
                    "gap": "none",
                    "align": "stretch",
                    "children": [pages[page_id]],
                },
                "events": {"pageChanged": "weather.page-changed"},
                "semantics": {"label": PAGE_LABELS[page_id]},
            },
        }
        result.append((json_path, cbor_path, document))
    return result


def expected_json(document: dict[str, object]) -> bytes:
    return (json.dumps(document, indent=2, ensure_ascii=False) + "\n").encode()


def generate() -> int:
    SCREENS.mkdir(parents=True, exist_ok=True)
    for json_path, cbor_path, document in documents():
        json_path.write_bytes(expected_json(document))
        cbor_path.write_bytes(compile_canonical_cbor(document))
        print(f"{json_path.relative_to(ROOT)}: {cbor_path.stat().st_size} bytes")
    return 0


def check() -> int:
    failures = []
    for json_path, cbor_path, document in documents():
        expected_cbor = compile_canonical_cbor(document)
        for path, expected in (
            (json_path, expected_json(document)),
            (cbor_path, expected_cbor),
        ):
            if not path.is_file() or path.read_bytes() != expected:
                failures.append(f"stale {path.relative_to(ROOT)}")
    if failures:
        print("\n".join(failures))
        return 1
    print("checked 5 bounded Weather AppSpecs")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true")
    mode.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    return generate() if arguments.generate else check()


if __name__ == "__main__":
    raise SystemExit(main())
