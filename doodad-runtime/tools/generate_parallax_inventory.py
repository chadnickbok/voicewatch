#!/usr/bin/env python3
"""Generate the frozen AppSpec/trace corpus inventory for Project Parallax."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

from doodad_cli.appspec import INTERACTIVE_TYPES, validate_appspec
from doodad_cli.parallax_contract import canonical_json_bytes
from doodad_cli.scene_trace import load_trace_bundle


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "apps" / "conformance-suite.json"
OUTPUT = ROOT / "reference" / "parallax-corpus-inventory.json"


def walk(node: dict[str, Any]) -> Iterable[dict[str, Any]]:
    yield node
    for child in node.get("props", {}).get("children", []):
        yield from walk(child)


def pattern(nodes: list[dict[str, Any]]) -> str:
    kinds = Counter(node["type"] for node in nodes)
    actions = sum(len(node.get("events", {})) for node in nodes)
    interactive = sum(
        node["type"] in INTERACTIVE_TYPES for node in nodes
    )
    if kinds["keypad"]:
        return "keypad"
    if kinds["progress"] and kinds["stepper"]:
        return "countdown"
    if (
        kinds["card"] == 1
        and kinds["button"] == 1
        and kinds["text"] == 4
    ):
        return "weather_hero"
    if kinds["scroll"] == 1 and kinds["column"] == 1 and kinds["card"]:
        return "calendar_agenda"
    if kinds["scroll"] == 1 and kinds["card"]:
        return "notification_stack"
    if kinds["scroll"] == 1 and kinds["toggle"]:
        return "task_list"
    if kinds["progress"]:
        return "progress_dashboard"
    if kinds["stepper"] or kinds["live_card"]:
        return "metric_control"
    if interactive == 0:
        return "empty"
    if kinds["card"] == 0 and actions >= 2:
        return "action_list"
    return "status_detail"


def source_documents(app_directory: Path) -> list[Path]:
    result = [app_directory / "appspec.json"]
    screens = app_directory / "screens"
    if screens.is_dir():
        result.extend(sorted(screens.glob("*.json")))
    return result


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate() -> dict[str, Any]:
    suite = json.loads(SUITE.read_text())["apps"]
    component_counts: Counter[str] = Counter()
    property_counts: Counter[str] = Counter()
    event_counts: Counter[str] = Counter()
    semantic_field_counts: Counter[str] = Counter()
    token_values: dict[str, Counter[str]] = {
        name: Counter()
        for name in (
            "variant",
            "style",
            "tone",
            "size",
            "gap",
            "align",
            "state",
        )
    }
    patterns: Counter[str] = Counter()
    documents_by_app: dict[str, int] = {}
    source_manifest = []
    dynamic_by_app = {}

    for app in suite:
        slug = app["slug"]
        paths = source_documents(ROOT / "apps" / slug)
        documents_by_app[slug] = len(paths)
        for path in paths:
            document = json.loads(path.read_text())
            validate_appspec(document)
            nodes = list(walk(document["screen"]))
            patterns[pattern(nodes)] += 1
            source_manifest.append(
                {
                    "path": path.relative_to(ROOT).as_posix(),
                    "sha256": digest(path),
                }
            )
            for node in nodes:
                kind = node["type"]
                component_counts[kind] += 1
                for key, value in node.get("props", {}).items():
                    if key == "children":
                        continue
                    property_counts[f"{kind}.{key}"] += 1
                    if key in token_values and isinstance(value, str):
                        token_values[key][value] += 1
                for event in node.get("events", {}):
                    event_counts[event] += 1
                for field in node.get("semantics", {}):
                    semantic_field_counts[field] += 1

        bundle = load_trace_bundle(
            ROOT / "reference" / "traces" / slug / "decisive"
        )
        entries = bundle.trace["entries"]
        checkpoints = bundle.checkpoints["checkpoints"]
        dynamic_by_app[slug] = {
            "accepted_operations": len(entries),
            "mounts": sum("mount" in entry for entry in entries),
            "command_batches": sum(
                "command_batch" in entry for entry in entries
            ),
            "checkpoints": len(checkpoints),
            "unique_snapshots": len(
                {
                    checkpoint["snapshot_sha256"]
                    for checkpoint in checkpoints
                }
            ),
            "screen_ids": sorted(
                {
                    checkpoint["screen_id"]
                    for checkpoint in checkpoints
                }
            ),
        }

    total_documents = sum(documents_by_app.values())
    return {
        "schema_version": 1,
        "suite": {
            "id": "doodad-20",
            "apps": [
                {
                    "index": app["index"],
                    "slug": app["slug"],
                    "id": app["id"],
                }
                for app in suite
            ],
            "app_count": len(suite),
            "document_count": total_documents,
        },
        "authored": {
            "documents_by_app": documents_by_app,
            "component_counts": dict(sorted(component_counts.items())),
            "property_counts": dict(sorted(property_counts.items())),
            "event_counts": dict(sorted(event_counts.items())),
            "semantic_field_counts": dict(
                sorted(semantic_field_counts.items())
            ),
            "token_values": {
                name: dict(sorted(values.items()))
                for name, values in token_values.items()
                if values
            },
            "patterns": dict(sorted(patterns.items())),
        },
        "runtime": {
            "apps": dynamic_by_app,
            "accepted_operations": sum(
                app["accepted_operations"]
                for app in dynamic_by_app.values()
            ),
            "checkpoints": sum(
                app["checkpoints"]
                for app in dynamic_by_app.values()
            ),
            "unique_snapshot_hashes": len(
                {
                    checkpoint["snapshot_sha256"]
                    for app in suite
                    for checkpoint in load_trace_bundle(
                        ROOT
                        / "reference"
                        / "traces"
                        / app["slug"]
                        / "decisive"
                    ).checkpoints["checkpoints"]
                }
            ),
        },
        "sources": source_manifest,
        "sources_sha256": hashlib.sha256(
            canonical_json_bytes(source_manifest)
        ).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    payload = json.dumps(
        generate(),
        ensure_ascii=False,
        indent=2,
        sort_keys=False,
    ) + "\n"
    if options.check:
        try:
            current = OUTPUT.read_text()
        except OSError as error:
            print(f"cannot read {OUTPUT}: {error}", file=sys.stderr)
            return 1
        if current != payload:
            print("Parallax corpus inventory is stale", file=sys.stderr)
            return 1
        print("Parallax corpus inventory is current")
        return 0
    OUTPUT.write_text(payload)
    print(f"Wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
