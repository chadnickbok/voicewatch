#!/usr/bin/env python3
"""Generate deterministic semantic and resource evidence for all 20 apps."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

from doodad_cli.contract import build_and_stage, read_json
from doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "apps" / "conformance-suite.json"
FLOWS = ROOT / "apps" / "conformance-flows.json"
EVIDENCE = ROOT / "evidence" / "conformance"

BUDGETS = {
    "maximum_module_bytes": 256 * 1024,
    "maximum_appspec_bytes": 4096,
    "maximum_semantic_nodes": 64,
    "maximum_lvgl_objects": 96,
    "maximum_lvgl_depth": 10,
    "maximum_decisive_actions": 12,
}


def canonical(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def changed_pixels(before: bytes | None, after: bytes) -> int:
    if before is None:
        return NativeHost.WIDTH * NativeHost.HEIGHT
    if len(before) != len(after) or len(after) % 2 != 0:
        raise RuntimeError("invalid RGB565 framebuffer length")
    return sum(
        before[offset : offset + 2] != after[offset : offset + 2]
        for offset in range(0, len(after), 2)
    )


def run_action(host: NativeHost, action: dict[str, Any]) -> None:
    kind = action["kind"]
    if kind == "click":
        host.click_button(str(action["value"]))
    elif kind == "advance":
        host.advance_time(int(action["value"]))
    elif kind == "deliver":
        host.deliver_provider()
    else:
        raise RuntimeError(f"unknown conformance action {kind!r}")


def stage_evidence(
    host: NativeHost,
    index: int,
    trigger: dict[str, Any] | None,
    prior_frame: bytes | None,
) -> tuple[dict[str, Any], bytes]:
    semantic = json.loads(host.semantic_snapshot())
    frame = host.framebuffer_rgb565()
    nodes = semantic["nodes"]
    root = nodes[0]
    stage = {
        "index": index,
        "trigger": trigger or {"kind": "start"},
        "screen_id": root["id"],
        "semantic_sha256": sha256(canonical(semantic)),
        "framebuffer_rgb565_sha256": sha256(frame),
        "changed_pixels": changed_pixels(prior_frame, frame),
        "mounted_nodes": host.mounted_node_count(),
        "mounted_events": host.mounted_event_count(),
        "lvgl_objects": host.lvgl_object_count(),
        "lvgl_depth": host.lvgl_max_depth(),
        "semantic_events": host.semantic_event_count(),
        "provider_requests": host.provider_request_count(),
        "semantic_tree": semantic,
    }
    if stage["mounted_nodes"] != len(nodes):
        raise RuntimeError(
            f"semantic node mismatch on {root['id']}: "
            f"{stage['mounted_nodes']} mounted vs {len(nodes)} serialized"
        )
    return stage, frame


def app_evidence(
    entry: dict[str, Any],
    actions: list[dict[str, Any]],
) -> dict[str, Any]:
    slug = entry["slug"]
    app_dir = ROOT / "apps" / slug
    package = build_and_stage(ROOT, app_dir)
    manifest = read_json(app_dir / "manifest.json")
    appspec_files = sorted(app_dir.glob("**/*.cbor"))
    authored_screens = sorted(app_dir.glob("**/*.json"))
    package_bytes = sum(
        path.stat().st_size
        for path in package.staging.iterdir()
        if path.is_file()
    )

    host = NativeHost(ROOT)
    stages: list[dict[str, Any]] = []
    prior_frame: bytes | None = None
    try:
        host.start_wasm(package.wasm)
        stage, prior_frame = stage_evidence(host, 0, None, prior_frame)
        stages.append(stage)
        for index, action in enumerate(actions, start=1):
            run_action(host, action)
            stage, prior_frame = stage_evidence(
                host, index, action, prior_frame
            )
            stages.append(stage)
    finally:
        host.close()

    maximum_nodes = max(stage["mounted_nodes"] for stage in stages)
    maximum_objects = max(stage["lvgl_objects"] for stage in stages)
    maximum_depth = max(stage["lvgl_depth"] for stage in stages)
    maximum_appspec = max(
        (path.stat().st_size for path in appspec_files),
        default=0,
    )
    summary = {
        "module_bytes": package.wasm.stat().st_size,
        "package_bytes": package_bytes,
        "authored_json_documents": len(authored_screens),
        "canonical_cbor_documents": len(appspec_files),
        "maximum_appspec_bytes": maximum_appspec,
        "decisive_actions": len(actions),
        "distinct_screens": len(
            {stage["screen_id"] for stage in stages}
        ),
        "distinct_semantic_states": len(
            {stage["semantic_sha256"] for stage in stages}
        ),
        "maximum_semantic_nodes": maximum_nodes,
        "maximum_lvgl_objects": maximum_objects,
        "maximum_lvgl_depth": maximum_depth,
        "total_changed_pixels": sum(
            stage["changed_pixels"] for stage in stages[1:]
        ),
        "semantic_events": stages[-1]["semantic_events"],
        "provider_requests": stages[-1]["provider_requests"],
    }
    failures = []
    for metric, limit in BUDGETS.items():
        measured_key = {
            "maximum_module_bytes": "module_bytes",
            "maximum_decisive_actions": "decisive_actions",
        }.get(metric, metric)
        if summary[measured_key] > limit:
            failures.append(
                f"{measured_key}={summary[measured_key]} > {limit}"
            )
    if summary["total_changed_pixels"] == 0:
        failures.append("decisive flow produced no visible frame change")
    if summary["distinct_semantic_states"] < 2:
        failures.append("decisive flow did not reach a second state")
    if failures:
        raise RuntimeError(f"{slug}: " + "; ".join(failures))

    return {
        "schema": 1,
        "app": {
            "slug": slug,
            "id": manifest["id"],
            "name": manifest["name"],
            "mode": entry["mode"],
            "surfaces": entry["surfaces"],
        },
        "budgets": BUDGETS,
        "summary": summary,
        "stages": stages,
    }


def render_document(value: dict[str, Any]) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        indent=2,
        sort_keys=False,
    ) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if checked-in evidence differs from a fresh run",
    )
    args = parser.parse_args()

    suite = read_json(SUITE)
    flows = read_json(FLOWS)
    entries = suite["apps"]
    flow_map = flows["flows"]
    slugs = [entry["slug"] for entry in entries]
    if sorted(slugs) != sorted(flow_map):
        raise RuntimeError(
            "conformance suite and decisive flow slugs differ"
        )

    failures: list[str] = []
    if not args.check:
        EVIDENCE.mkdir(parents=True, exist_ok=True)
    for entry in entries:
        slug = entry["slug"]
        print(f"evidence: {slug}", flush=True)
        document = render_document(
            app_evidence(entry, flow_map[slug])
        )
        output = EVIDENCE / f"{slug}.json"
        if args.check:
            try:
                existing = output.read_text(encoding="utf-8")
            except FileNotFoundError:
                failures.append(f"missing {output.relative_to(ROOT)}")
                continue
            if existing != document:
                failures.append(
                    f"stale {output.relative_to(ROOT)}"
                )
        else:
            output.write_text(document, encoding="utf-8")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    verb = "verified" if args.check else "generated"
    print(f"{verb} semantic/resource evidence for {len(entries)} apps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
