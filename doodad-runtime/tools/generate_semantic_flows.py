#!/usr/bin/env python3
"""Migrate decisive flows from visible labels to semantic action identity."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
FLOWS = ROOT / "apps" / "conformance-flows.json"
TRACES = ROOT / "reference" / "traces"


def semantic_action(
    slug: str,
    stage_index: int,
    prior_revision: int,
    after_revision: int,
) -> dict[str, Any]:
    trace = json.loads(
        (TRACES / slug / "decisive" / "trace.json").read_text()
    )
    causes = {
        json.dumps(
            entry["cause"],
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        )
        for entry in trace["entries"]
        if (
            prior_revision < entry["scene_revision"] <= after_revision
            and entry["cause"]["kind"] == "semantic_action"
        )
    }
    if len(causes) != 1:
        raise RuntimeError(
            f"{slug} stage {stage_index} has {len(causes)} semantic causes"
        )
    cause = json.loads(causes.pop())
    action = {
        "kind": "semantic",
        "node_id": cause["node_id"],
        "action_id": cause["action_id"],
        "event_kind": cause["event_kind"],
    }
    if "typed_value" in cause:
        action["typed_value"] = cause["typed_value"]
    return action


def main() -> None:
    source = json.loads(FLOWS.read_text())
    migrated: dict[str, list[dict[str, Any]]] = {}
    for slug, actions in source["flows"].items():
        checkpoints = json.loads(
            (
                TRACES
                / slug
                / "decisive"
                / "checkpoints.json"
            ).read_text()
        )["checkpoints"]
        output = []
        for stage_index, action in enumerate(actions, start=1):
            if action["kind"] != "click":
                output.append(action)
                continue
            output.append(
                semantic_action(
                    slug,
                    stage_index,
                    int(checkpoints[stage_index - 1]["after_revision"]),
                    int(checkpoints[stage_index]["after_revision"]),
                )
            )
        migrated[slug] = output
    document = {
        "schema": 2,
        "action_identity": "node_action_event",
        "flows": migrated,
    }
    FLOWS.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Migrated decisive flows for {len(migrated)} apps")


if __name__ == "__main__":
    main()
