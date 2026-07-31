from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from .contract import (
    DoodadError,
    build_and_stage,
    find_project_root,
    read_json,
)
from .native import NativeHost, SceneOperation
from .parallax_contract import (
    canonical_json_bytes,
    document_sha256,
    validate_scene_snapshot,
    validate_scene_trace,
)


CAUSE_NAMES = {
    0: "start",
    1: "semantic_action",
    2: "provider_event",
    3: "timer",
}
OPERATION_FIELDS = {
    0: "mount",
    1: "command_batch",
}
EVENT_NAMES = (
    "tap",
    "long_press",
    "repeat",
    "value_changing",
    "value_committed",
    "checked_changed",
    "page_changed",
    "dismissed",
    "submit",
    "retry",
    "cancel",
)


@dataclass(frozen=True)
class TraceBundle:
    directory: Path
    trace: dict[str, Any]
    checkpoints: dict[str, Any]


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _canonical_document(value: Any) -> bytes:
    return canonical_json_bytes(value) + b"\n"


def _atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)


def _content_artifact(
    directory: Path,
    folder: str,
    suffix: str,
    payload: bytes,
) -> dict[str, Any]:
    digest = sha256_bytes(payload)
    relative = Path(folder) / f"{digest}{suffix}"
    output = directory / relative
    if output.is_file():
        if output.read_bytes() != payload:
            raise DoodadError(f"content-address collision at {output}")
    else:
        _atomic_write(output, payload)
    return {
        "path": relative.as_posix(),
        "sha256": digest,
        "bytes": len(payload),
    }


def _read_artifact(directory: Path, artifact: dict[str, Any]) -> bytes:
    path = directory / str(artifact["path"])
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise DoodadError(f"cannot read trace artifact {path}: {error}") from error
    digest = sha256_bytes(payload)
    if digest != artifact["sha256"] or len(payload) != artifact["bytes"]:
        raise DoodadError(f"stale or corrupt trace artifact: {path}")
    return payload


def _paths_hash(root: Path, paths: Iterable[Path]) -> str:
    records: list[dict[str, str]] = []
    unique: dict[str, Path] = {}
    for path in paths:
        resolved = path.resolve()
        if resolved.is_dir():
            for child in resolved.rglob("*"):
                if child.is_file():
                    relative = child.resolve().relative_to(root.resolve()).as_posix()
                    unique[relative] = child
        elif resolved.is_file():
            relative = resolved.relative_to(root.resolve()).as_posix()
            unique[relative] = resolved
    for relative, path in sorted(unique.items()):
        records.append(
            {
                "path": relative,
                "sha256": sha256_bytes(path.read_bytes()),
            }
        )
    return sha256_bytes(canonical_json_bytes(records))


def _package_hash(staging: Path) -> str:
    records = []
    for path in sorted(staging.rglob("*")):
        if path.is_file():
            records.append(
                {
                    "path": path.relative_to(staging).as_posix(),
                    "sha256": sha256_bytes(path.read_bytes()),
                }
            )
    return sha256_bytes(canonical_json_bytes(records))


def trace_environment(root: Path) -> dict[str, Any]:
    component_sources = [
        root / "components" / "m3e_lvgl",
        root / "tools" / "native-host" / "include",
        root / "tools" / "native-host" / "src",
        root / "tools" / "native-host" / "CMakeLists.txt",
        root / "tools" / "native-host" / "lv_conf.h",
        root / "ui" / "doodad_lvgl_ui.c",
        root / "ui" / "doodad_lvgl_ui.h",
        root / "firmware" / "dependencies.lock",
        root / "tools" / "doodad_cli" / "scene_trace.py",
        root / "tools" / "doodad_cli" / "native.py",
        root / "tools" / "doodad_cli" / "parallax_contract.py",
        root / "contracts" / "scene-trace-v1.schema.json",
        root / "contracts" / "scene-snapshot-v1.schema.json",
    ]
    return {
        "profile_id": "watch_square_240",
        "locale": "en-US",
        "timezone": "America/Los_Angeles",
        "font_scale_milli": 1000,
        "reduced_motion": False,
        "origin": "guest_appspec",
        "versions": {
            "wamr": "2.4.0",
            "lvgl": "9.5.0",
            "host_abi": "1",
            "appspec": "1.2",
            "component_set": "1",
            "simulator": "parallax-v1",
        },
        "hashes": {
            "interpretation_policy": sha256_bytes(
                (root / "reference" / "interpretation-policy-v1.json").read_bytes()
            ),
            "theme": _paths_hash(
                root,
                [
                    root / "reference" / "material-tokens",
                    root
                    / "components"
                    / "m3e_lvgl"
                    / "include"
                    / "m3e"
                    / "generated"
                    / "core_tokens.hpp",
                ],
            ),
            "font": _paths_hash(
                root,
                [
                    root / "tools" / "native-host" / "lv_conf.h",
                    root / "firmware" / "dependencies.lock",
                ],
            ),
            "icons": sha256_bytes(b"doodad-semantic-icon-names-v1\n"),
            "simulator_build": _paths_hash(root, component_sources),
        },
    }


def _decode_cbor(payload: bytes) -> Any:
    offset = 0

    def read_argument(additional: int) -> int:
        nonlocal offset
        if additional < 24:
            return additional
        widths = {24: 1, 25: 2, 26: 4, 27: 8}
        width = widths.get(additional)
        if width is None or offset + width > len(payload):
            raise DoodadError("unsupported or truncated trace CBOR")
        value = int.from_bytes(payload[offset : offset + width], "big")
        offset += width
        return value

    def read_value() -> Any:
        nonlocal offset
        if offset >= len(payload):
            raise DoodadError("truncated trace CBOR")
        initial = payload[offset]
        offset += 1
        major = initial >> 5
        additional = initial & 0x1F
        if major in {0, 1}:
            value = read_argument(additional)
            return value if major == 0 else -1 - value
        if major == 3:
            length = read_argument(additional)
            if offset + length > len(payload):
                raise DoodadError("truncated trace CBOR string")
            value = payload[offset : offset + length].decode("utf-8")
            offset += length
            return value
        if major == 4:
            return [read_value() for _ in range(read_argument(additional))]
        if major == 5:
            return {
                read_value(): read_value()
                for _ in range(read_argument(additional))
            }
        if major == 7 and additional in {20, 21, 22}:
            return {20: False, 21: True, 22: None}[additional]
        raise DoodadError("unsupported trace CBOR value")

    result = read_value()
    if offset != len(payload):
        raise DoodadError("trace CBOR has trailing bytes")
    return result


def _cause(record: SceneOperation) -> dict[str, Any]:
    cause_name = CAUSE_NAMES.get(record.cause_kind)
    if cause_name is None:
        raise DoodadError(f"unknown native trace cause {record.cause_kind}")
    if cause_name == "start":
        return {"kind": "start"}
    if cause_name == "semantic_action":
        event = _decode_cbor(record.cause)
        if not isinstance(event, dict):
            raise DoodadError("semantic event trace cause is not a CBOR map")
        try:
            event_kind = EVENT_NAMES[int(event[5])]
            cause = {
                "kind": "semantic_action",
                "node_id": str(event[3]),
                "action_id": str(event[4]),
                "event_kind": event_kind,
            }
        except (KeyError, IndexError, TypeError, ValueError) as error:
            raise DoodadError("semantic event trace cause is malformed") from error
        if 7 in event:
            cause["typed_value"] = event[7]
        return cause
    if cause_name == "provider_event":
        return {
            "kind": "provider_event",
            "provider": "mock.provider",
            "event": "provider.delivery",
        }
    return {
        "kind": "timer",
        "event": "timer.delivery",
    }


def run_flow_action(host: NativeHost, action: dict[str, Any]) -> None:
    kind = action["kind"]
    if kind == "semantic":
        host.dispatch_semantic_action(
            str(action["node_id"]),
            str(action["action_id"]),
            str(action["event_kind"]),
            action.get("typed_value"),
        )
    elif kind == "click":
        # Legacy input remains readable solely for migration.
        host.click_button(str(action["value"]))
    elif kind == "advance":
        host.advance_time(int(action["value"]))
    elif kind == "deliver":
        host.deliver_provider()
    else:
        raise DoodadError(f"unknown conformance action {kind!r}")


def _checkpoint(
    host: NativeHost,
    stage_index: int,
    trigger: dict[str, Any],
) -> dict[str, Any]:
    snapshot = json.loads(host.scene_snapshot())
    validate_scene_snapshot(snapshot)
    semantic = json.loads(host.semantic_snapshot())
    framebuffer = host.framebuffer_rgb565()
    return {
        "stage_index": stage_index,
        "trigger": trigger,
        "after_revision": host.scene_revision(),
        "route_generation": host.route_generation(),
        "scenario_time_ms": host.scenario_time(),
        "screen_id": snapshot["screen_id"],
        "snapshot_sha256": document_sha256(snapshot),
        "semantic_sha256": document_sha256(semantic),
        "framebuffer_rgb565_sha256": sha256_bytes(framebuffer),
        "mounted_nodes": host.mounted_node_count(),
        "mounted_events": host.mounted_event_count(),
    }


def _trace_entries(
    directory: Path,
    records: list[SceneOperation],
) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    prior_snapshot: str | None = None
    for sequence, record in enumerate(records):
        if record.snapshot_json is None:
            raise DoodadError(
                f"native trace operation {sequence} omitted its scene snapshot"
            )
        snapshot = json.loads(record.snapshot_json)
        validate_scene_snapshot(snapshot)
        snapshot_payload = canonical_json_bytes(snapshot)
        snapshot_artifact = _content_artifact(
            directory,
            "snapshots",
            ".json",
            snapshot_payload,
        )
        operation_field = OPERATION_FIELDS.get(record.operation_kind)
        if operation_field is None:
            raise DoodadError(
                f"unknown native operation kind {record.operation_kind}"
            )
        operation_artifact = _content_artifact(
            directory,
            "objects",
            ".cbor",
            record.operation,
        )
        committed = record.outcome == 0
        after_snapshot = (
            snapshot_artifact["sha256"] if committed else prior_snapshot
        )
        entry: dict[str, Any] = {
            "sequence": sequence,
            "scenario_time_ms": record.scenario_time_ms,
            "cause": _cause(record),
            "outcome": "committed" if committed else "rejected",
            "scene_revision": record.scene_revision,
            "route_generation": record.route_generation,
            "screen_id": snapshot["screen_id"],
            "before_snapshot_sha256": prior_snapshot,
            "after_snapshot_sha256": after_snapshot,
            operation_field: operation_artifact,
        }
        if record.cause:
            entry["cause_payload"] = _content_artifact(
                directory,
                "causes",
                ".cbor",
                record.cause,
            )
        if committed:
            entry["snapshot"] = snapshot_artifact
            prior_snapshot = after_snapshot
        else:
            entry["failure"] = "native renderer rejected operation"
        entries.append(entry)
    return entries


def record_flow_trace(
    root: Path,
    slug: str,
    actions: list[dict[str, Any]],
    output_directory: Path,
) -> TraceBundle:
    root = root.resolve()
    output_directory = output_directory.resolve()
    app_directory = root / "apps" / slug
    package = build_and_stage(root, app_directory)
    manifest_path = app_directory / "manifest.json"
    manifest = read_json(manifest_path)

    host = NativeHost(root)
    checkpoints: list[dict[str, Any]] = []
    records: list[SceneOperation] = []
    try:
        host.start_wasm(package.wasm)
        checkpoints.append(
            _checkpoint(host, 0, {"kind": "start"})
        )
        for index, action in enumerate(actions, start=1):
            run_flow_action(host, action)
            checkpoints.append(_checkpoint(host, index, dict(action)))
        records = host.scene_operations()
    finally:
        host.close()

    trace = {
        "schema_version": 1,
        "id": f"{slug}.decisive",
        "app": {
            "slug": slug,
            "id": manifest["id"],
            "package_sha256": _package_hash(package.staging),
            "wasm_sha256": sha256_bytes(package.wasm.read_bytes()),
            "manifest_sha256": sha256_bytes(manifest_path.read_bytes()),
        },
        "environment": trace_environment(root),
        "scenario_id": f"{slug}.decisive",
        "entries": _trace_entries(output_directory, records),
    }
    validate_scene_trace(trace)
    checkpoint_document = {
        "schema_version": 1,
        "trace_sha256": document_sha256(trace),
        "app": slug,
        "checkpoints": checkpoints,
    }
    _validate_checkpoints(checkpoint_document)
    _atomic_write(
        output_directory / "trace.json",
        _canonical_document(trace),
    )
    _atomic_write(
        output_directory / "checkpoints.json",
        _canonical_document(checkpoint_document),
    )
    return TraceBundle(output_directory, trace, checkpoint_document)


def _validate_checkpoints(document: dict[str, Any]) -> None:
    if set(document) != {
        "schema_version",
        "trace_sha256",
        "app",
        "checkpoints",
    }:
        raise DoodadError("trace checkpoints have an invalid shape")
    if document["schema_version"] != 1:
        raise DoodadError("trace checkpoint schema version is unsupported")
    checkpoints = document["checkpoints"]
    if not isinstance(checkpoints, list) or not checkpoints:
        raise DoodadError("trace checkpoints must be a non-empty array")
    expected_keys = {
        "stage_index",
        "trigger",
        "after_revision",
        "route_generation",
        "scenario_time_ms",
        "screen_id",
        "snapshot_sha256",
        "semantic_sha256",
        "framebuffer_rgb565_sha256",
        "mounted_nodes",
        "mounted_events",
    }
    for index, checkpoint in enumerate(checkpoints):
        if not isinstance(checkpoint, dict) or set(checkpoint) != expected_keys:
            raise DoodadError(f"checkpoint {index} has an invalid shape")
        if checkpoint["stage_index"] != index:
            raise DoodadError("trace checkpoint indices are not contiguous")
        for key in (
            "snapshot_sha256",
            "semantic_sha256",
            "framebuffer_rgb565_sha256",
        ):
            value = checkpoint[key]
            if (
                not isinstance(value, str)
                or len(value) != 64
                or any(character not in "0123456789abcdef" for character in value)
            ):
                raise DoodadError(f"checkpoint {index} has invalid {key}")


def load_trace_bundle(directory: Path) -> TraceBundle:
    directory = directory.resolve()
    trace = read_json(directory / "trace.json")
    checkpoints = read_json(directory / "checkpoints.json")
    validate_scene_trace(trace)
    _validate_checkpoints(checkpoints)
    if checkpoints["trace_sha256"] != document_sha256(trace):
        raise DoodadError("trace checkpoints refer to a different trace")
    for entry in trace["entries"]:
        for field in (
            "cause_payload",
            "mount",
            "command_batch",
            "snapshot",
        ):
            if field in entry:
                _read_artifact(directory, entry[field])
    return TraceBundle(directory, trace, checkpoints)


def _capture_replay_checkpoint(host: NativeHost) -> dict[str, Any]:
    snapshot = json.loads(host.scene_snapshot())
    semantic = json.loads(host.semantic_snapshot())
    return {
        "after_revision": host.scene_revision(),
        "route_generation": host.route_generation(),
        "scenario_time_ms": host.scenario_time(),
        "screen_id": snapshot["screen_id"],
        "snapshot_sha256": document_sha256(snapshot),
        "semantic_sha256": document_sha256(semantic),
        "framebuffer_rgb565_sha256": sha256_bytes(
            host.framebuffer_rgb565()
        ),
        "mounted_nodes": host.mounted_node_count(),
        "mounted_events": host.mounted_event_count(),
    }


def replay_trace_bundle(directory: Path) -> dict[str, Any]:
    bundle = load_trace_bundle(directory)
    checkpoints_by_revision: dict[int, list[dict[str, Any]]] = {}
    for checkpoint in bundle.checkpoints["checkpoints"]:
        checkpoints_by_revision.setdefault(
            int(checkpoint["after_revision"]),
            [],
        ).append(checkpoint)
    verified_checkpoints: set[int] = set()

    host = NativeHost(find_project_root(bundle.directory))
    try:
        for entry in bundle.trace["entries"]:
            operation_field = (
                "mount" if "mount" in entry else "command_batch"
            )
            operation = _read_artifact(
                bundle.directory,
                entry[operation_field],
            )
            rejected = entry["outcome"] == "rejected"
            try:
                if operation_field == "mount":
                    host.replay_mount(
                        operation,
                        int(entry["scenario_time_ms"]),
                    )
                else:
                    host.replay_command_batch(
                        operation,
                        int(entry["scenario_time_ms"]),
                    )
            except DoodadError:
                if not rejected:
                    raise
            else:
                if rejected:
                    raise DoodadError(
                        f"replay accepted rejected entry {entry['sequence']}"
                    )
            if host.scene_revision() != entry["scene_revision"]:
                raise DoodadError(
                    f"replay revision mismatch at entry {entry['sequence']}"
                )
            if host.route_generation() != entry["route_generation"]:
                raise DoodadError(
                    f"replay route mismatch at entry {entry['sequence']}"
                )
            if entry["after_snapshot_sha256"] is not None:
                current_snapshot = json.loads(host.scene_snapshot())
                if (
                    document_sha256(current_snapshot)
                    != entry["after_snapshot_sha256"]
                ):
                    raise DoodadError(
                        f"replay snapshot mismatch at entry {entry['sequence']}"
                    )

            revision = host.scene_revision()
            if revision not in checkpoints_by_revision:
                continue
            actual = _capture_replay_checkpoint(host)
            for expected in checkpoints_by_revision[revision]:
                comparable = {
                    key: expected[key]
                    for key in actual
                }
                if actual != comparable:
                    differences = sorted(
                        key
                        for key in actual
                        if actual[key] != comparable[key]
                    )
                    raise DoodadError(
                        f"replay checkpoint {expected['stage_index']} "
                        f"differs in {differences}"
                    )
                verified_checkpoints.add(int(expected["stage_index"]))
        expected_indices = {
            int(checkpoint["stage_index"])
            for checkpoint in bundle.checkpoints["checkpoints"]
        }
        if verified_checkpoints != expected_indices:
            missing = sorted(expected_indices - verified_checkpoints)
            raise DoodadError(f"replay missed checkpoints {missing}")
        if host.wasm_call_count() != 0:
            raise DoodadError("trace replay invoked WebAssembly")
    finally:
        host.close()

    return {
        "schema_version": 1,
        "trace_sha256": document_sha256(bundle.trace),
        "entries": len(bundle.trace["entries"]),
        "checkpoints": len(bundle.checkpoints["checkpoints"]),
        "wasm_calls": 0,
        "passed": True,
    }


def verify_trace_bundle_fresh(
    root: Path,
    directory: Path,
) -> dict[str, Any]:
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.doodad_cli.scene_trace",
            "--replay",
            str(directory.resolve()),
        ],
        cwd=root.resolve(),
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise DoodadError(f"fresh-process trace replay failed: {detail}")
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise DoodadError(
            f"fresh replay returned invalid JSON: {result.stdout!r}"
        ) from error
    if document.get("passed") is not True:
        raise DoodadError("fresh-process trace replay did not pass")
    return document


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--replay", type=Path, required=True)
    options = parser.parse_args(arguments)
    try:
        result = replay_trace_bundle(options.replay)
    except (DoodadError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(canonical_json_bytes(result).decode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
