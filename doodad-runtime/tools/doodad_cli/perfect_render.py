from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from .contract import DoodadError, find_project_root, read_json
from .native import NativeHost
from .parallax_contract import (
    canonical_json_bytes,
    document_sha256,
    validate_node_evidence,
    validate_perfect_render_suite,
    validate_scene_snapshot,
)
from .rgb565 import DEFAULT_HEIGHT, DEFAULT_WIDTH, rgb565le_to_rgb888
from .scene_trace import TraceBundle, load_trace_bundle


@dataclass(frozen=True)
class ReplayOperation:
    sequence: int
    kind: str
    outcome: str
    scene_revision: int
    route_generation: int
    scenario_time_ms: int
    payload: bytes
    payload_sha256: str
    after_snapshot_sha256: str | None


@dataclass(frozen=True)
class PerfectRenderSelection:
    suite_path: Path
    suite_id: str
    suite_sha256: str
    entry_index: int
    entry: dict[str, Any]
    bundle: TraceBundle
    trace_sha256: str
    checkpoints_sha256: str
    target_entry: dict[str, Any]
    snapshot: dict[str, Any]
    operations: tuple[ReplayOperation, ...]
    checkpoint: dict[str, Any] | None


HostFactory = Callable[[Path], NativeHost]


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_perfect_render_suite(path: Path) -> dict[str, Any]:
    suite = read_json(path.resolve())
    validate_perfect_render_suite(suite)
    return suite


def resolve_suite_entries(
    suite_path: Path,
    *,
    app_slug: str | None = None,
) -> list[PerfectRenderSelection]:
    suite_path = suite_path.resolve()
    suite = load_perfect_render_suite(suite_path)
    suite_sha256 = document_sha256(suite)
    project_root = find_project_root(suite_path)
    selections: list[PerfectRenderSelection] = []

    for entry_index, entry in enumerate(suite["entries"]):
        if app_slug is not None and entry["app_slug"] != app_slug:
            continue
        selections.append(
            _resolve_entry(
                project_root,
                suite_path,
                suite,
                suite_sha256,
                entry_index,
                entry,
            )
        )

    if app_slug is not None and not selections:
        raise DoodadError(
            f"suite {suite['id']!r} does not select app {app_slug!r}"
        )
    return selections


def replay_to_selected_revision(
    selection: PerfectRenderSelection,
    host: NativeHost,
) -> None:
    if host.wasm_call_count() != 0:
        raise DoodadError("LVGL replay host already contains WebAssembly calls")

    for operation in selection.operations:
        rejected = operation.outcome == "rejected"
        try:
            if operation.kind == "mount":
                host.replay_mount(
                    operation.payload,
                    operation.scenario_time_ms,
                )
            else:
                host.replay_command_batch(
                    operation.payload,
                    operation.scenario_time_ms,
                )
        except DoodadError:
            if not rejected:
                raise
        else:
            if rejected:
                raise DoodadError(
                    f"LVGL replay accepted rejected trace entry "
                    f"{operation.sequence}"
                )

        if host.scene_revision() != operation.scene_revision:
            raise DoodadError(
                f"LVGL replay revision mismatch at trace entry "
                f"{operation.sequence}"
            )
        if host.route_generation() != operation.route_generation:
            raise DoodadError(
                f"LVGL replay route mismatch at trace entry "
                f"{operation.sequence}"
            )
        if operation.after_snapshot_sha256 is not None:
            actual_snapshot = json.loads(host.scene_snapshot())
            if (
                document_sha256(actual_snapshot)
                != operation.after_snapshot_sha256
            ):
                raise DoodadError(
                    f"LVGL replay snapshot mismatch at trace entry "
                    f"{operation.sequence}"
                )
        if host.wasm_call_count() != 0:
            raise DoodadError("LVGL trace replay invoked WebAssembly")

    target = selection.target_entry
    if host.scene_revision() != target["scene_revision"]:
        raise DoodadError("LVGL replay did not reach the selected revision")
    if host.route_generation() != target["route_generation"]:
        raise DoodadError("LVGL replay did not reach the selected route")
    if host.scenario_time() != target["scenario_time_ms"]:
        raise DoodadError("LVGL replay did not reach the selected scenario time")


def capture_lvgl_entry(
    project_root: Path,
    selection: PerfectRenderSelection,
    output_root: Path,
    *,
    host_factory: HostFactory = NativeHost,
) -> dict[str, Any]:
    project_root = find_project_root(project_root)
    output_directory = entry_output_directory(output_root, selection.entry)
    host = host_factory(project_root)
    try:
        replay_to_selected_revision(selection, host)
        snapshot = json.loads(host.scene_snapshot())
        validate_scene_snapshot(snapshot)
        snapshot_sha256 = document_sha256(snapshot)
        if snapshot_sha256 != selection.entry["snapshot_sha256"]:
            raise DoodadError(
                "LVGL renderer did not attest to the suite SceneSnapshot"
            )
        if snapshot != selection.snapshot:
            raise DoodadError(
                "LVGL renderer snapshot differs from the selected trace artifact"
            )

        semantic = json.loads(host.semantic_snapshot())
        semantic_sha256 = document_sha256(semantic)
        framebuffer = host.framebuffer_rgb565()
        if host.WIDTH != DEFAULT_WIDTH or host.HEIGHT != DEFAULT_HEIGHT:
            raise DoodadError(
                "LVGL simulator does not match the 240-square capture contract"
            )
        # The canonical conversion helper validates the byte count, word
        # endianness, and top-to-bottom row-major buffer contract.
        rgb565le_to_rgb888(
            framebuffer,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        framebuffer_sha256 = sha256_bytes(framebuffer)
        capture_phase = _capture_phase(selection.entry["capture_phase"])
        node_evidence = host.node_evidence(capture_phase)
        validate_node_evidence(node_evidence)
        if node_evidence["snapshot_sha256"] != snapshot_sha256:
            raise DoodadError(
                "LVGL node evidence refers to a different SceneSnapshot"
            )
        if node_evidence["profile_id"] != selection.entry["profile_id"]:
            raise DoodadError(
                "LVGL node evidence uses a different display profile"
            )
        expected_renderer = selection.entry["lvgl"]
        actual_renderer = node_evidence["renderer"]
        if (
            actual_renderer["kind"] != "lvgl"
            or actual_renderer["mode"] != expected_renderer["mode"]
            or actual_renderer["version"] != expected_renderer["version"]
        ):
            raise DoodadError(
                "LVGL node evidence does not match the suite renderer pin"
            )
        snapshot_ids = [node["id"] for node in snapshot["nodes"]]
        evidence_ids = [node["id"] for node in node_evidence["nodes"]]
        if evidence_ids != snapshot_ids:
            raise DoodadError(
                "LVGL node evidence does not cover the selected scene in order"
            )

        runtime = {
            "scene_revision": host.scene_revision(),
            "route_generation": host.route_generation(),
            "scenario_time_ms": host.scenario_time(),
            "mounted_nodes": host.mounted_node_count(),
            "mounted_events": host.mounted_event_count(),
            "lvgl_objects": host.lvgl_object_count(),
            "lvgl_max_depth": host.lvgl_max_depth(),
        }
        checkpoint_attestation = _checkpoint_attestation(
            selection,
            runtime,
            snapshot,
            semantic_sha256,
            framebuffer_sha256,
        )
        wasm_calls = host.wasm_call_count()
        if wasm_calls != 0:
            raise DoodadError("LVGL perfect-render capture invoked WebAssembly")
    finally:
        host.close()

    snapshot_payload = canonical_json_bytes(snapshot)
    evidence_payload = canonical_json_bytes(node_evidence)
    artifacts = {
        "scene_snapshot": _artifact(
            "scene-snapshot.json",
            snapshot_payload,
        ),
        "framebuffer_rgb565le": _artifact(
            "lvgl.rgb565le",
            framebuffer,
        ),
        "node_evidence": _artifact(
            "lvgl-nodes.json",
            evidence_payload,
        ),
    }
    manifest = {
        "schema_version": 1,
        "kind": "parallax-lvgl-capture",
        "suite": {
            "id": selection.suite_id,
            "sha256": selection.suite_sha256,
        },
        "selection": {
            "app_slug": selection.entry["app_slug"],
            "trace": selection.entry["trace"],
            "trace_sha256": selection.trace_sha256,
            "checkpoints_sha256": selection.checkpoints_sha256,
            "sequence": selection.target_entry["sequence"],
            "scene_revision": selection.target_entry["scene_revision"],
            "scenario_id": selection.bundle.trace["scenario_id"],
            "capture_phase": selection.entry["capture_phase"],
            "profile_id": selection.entry["profile_id"],
            "snapshot_sha256": selection.entry["snapshot_sha256"],
        },
        "renderer": node_evidence["renderer"],
        "framebuffer": {
            "format": "rgb565le",
            "physical_width_px": DEFAULT_WIDTH,
            "physical_height_px": DEFAULT_HEIGHT,
            "row_order": "top_to_bottom",
        },
        "runtime": runtime,
        "replay": [
            {
                "sequence": operation.sequence,
                "kind": operation.kind,
                "outcome": operation.outcome,
                "scene_revision": operation.scene_revision,
                "route_generation": operation.route_generation,
                "scenario_time_ms": operation.scenario_time_ms,
                "payload_sha256": operation.payload_sha256,
            }
            for operation in selection.operations
        ],
        "hashes": {
            "snapshot_sha256": snapshot_sha256,
            "semantic_sha256": semantic_sha256,
            "framebuffer_rgb565_sha256": framebuffer_sha256,
            "node_evidence_sha256": document_sha256(node_evidence),
            "simulator_source_sha256": selection.bundle.trace[
                "environment"
            ]["hashes"]["simulator_build"],
        },
        "artifacts": artifacts,
        "attestations": {
            "trace_contract_valid": True,
            "suite_snapshot_hash_shared": True,
            "renderer_snapshot_hash_shared": True,
            "checkpoint_present": selection.checkpoint is not None,
            "checkpoint_matched": checkpoint_attestation["matched"],
            "checkpoint_differences": checkpoint_attestation["differences"],
            "wasm_call_count": wasm_calls,
        },
        "trace_checkpoint": checkpoint_attestation,
    }

    output_directory.mkdir(parents=True, exist_ok=True)
    _atomic_write(
        output_directory / "scene-snapshot.json",
        snapshot_payload,
    )
    _atomic_write(
        output_directory / "lvgl.rgb565le",
        framebuffer,
    )
    _atomic_write(
        output_directory / "lvgl-nodes.json",
        evidence_payload,
    )
    _atomic_write(
        output_directory / "manifest.json",
        canonical_json_bytes(manifest),
    )
    return manifest


def capture_lvgl_suite(
    project_root: Path,
    suite_path: Path,
    output_root: Path,
    *,
    app_slug: str | None = None,
    host_factory: HostFactory = NativeHost,
) -> list[dict[str, Any]]:
    selections = resolve_suite_entries(suite_path, app_slug=app_slug)
    return [
        capture_lvgl_entry(
            project_root,
            selection,
            output_root,
            host_factory=host_factory,
        )
        for selection in selections
    ]


def entry_output_directory(
    output_root: Path,
    entry: dict[str, Any],
) -> Path:
    return (
        output_root.resolve()
        / entry["app_slug"]
        / entry["capture_phase"]
        / entry["profile_id"]
        / f"sequence-{int(entry['sequence']):04d}"
    )


def _resolve_entry(
    project_root: Path,
    suite_path: Path,
    suite: dict[str, Any],
    suite_sha256: str,
    entry_index: int,
    entry: dict[str, Any],
) -> PerfectRenderSelection:
    trace_path = (suite_path.parent / entry["trace"]).resolve()
    try:
        trace_path.relative_to(project_root)
    except ValueError as error:
        raise DoodadError(
            f"suite entry {entry_index} trace escapes the project"
        ) from error
    if trace_path.name != "trace.json":
        raise DoodadError(
            f"suite entry {entry_index} must refer to trace.json"
        )

    bundle = load_trace_bundle(trace_path.parent)
    trace = bundle.trace
    if trace["app"]["slug"] != entry["app_slug"]:
        raise DoodadError(
            f"suite entry {entry_index} app does not match its trace"
        )
    if trace["environment"]["profile_id"] != entry["profile_id"]:
        raise DoodadError(
            f"suite entry {entry_index} profile does not match its trace"
        )
    if trace["environment"]["versions"]["lvgl"] != entry["lvgl"]["version"]:
        raise DoodadError(
            f"suite entry {entry_index} LVGL version does not match its trace"
        )

    sequence = int(entry["sequence"])
    if sequence >= len(trace["entries"]):
        raise DoodadError(
            f"suite entry {entry_index} sequence is outside its trace"
        )
    target_entry = trace["entries"][sequence]
    if target_entry["sequence"] != sequence:
        raise DoodadError(
            f"suite entry {entry_index} sequence is not contiguous"
        )
    if target_entry["outcome"] != "committed":
        raise DoodadError(
            f"suite entry {entry_index} does not select a committed scene"
        )
    if target_entry["after_snapshot_sha256"] != entry["snapshot_sha256"]:
        raise DoodadError(
            f"suite entry {entry_index} SceneSnapshot hash is stale"
        )

    snapshot_artifact = _find_snapshot_artifact(
        trace["entries"][: sequence + 1],
        entry["snapshot_sha256"],
    )
    snapshot_payload = _read_artifact(bundle.directory, snapshot_artifact)
    try:
        snapshot = json.loads(snapshot_payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DoodadError(
            f"suite entry {entry_index} snapshot artifact is invalid JSON"
        ) from error
    validate_scene_snapshot(snapshot)
    if document_sha256(snapshot) != entry["snapshot_sha256"]:
        raise DoodadError(
            f"suite entry {entry_index} snapshot content hash is stale"
        )

    operations = tuple(
        _resolve_operation(bundle, trace_entry)
        for trace_entry in trace["entries"][: sequence + 1]
    )
    checkpoint = _resolve_checkpoint(bundle, target_entry)
    return PerfectRenderSelection(
        suite_path=suite_path,
        suite_id=suite["id"],
        suite_sha256=suite_sha256,
        entry_index=entry_index,
        entry=entry,
        bundle=bundle,
        trace_sha256=document_sha256(trace),
        checkpoints_sha256=document_sha256(bundle.checkpoints),
        target_entry=target_entry,
        snapshot=snapshot,
        operations=operations,
        checkpoint=checkpoint,
    )


def _resolve_operation(
    bundle: TraceBundle,
    entry: dict[str, Any],
) -> ReplayOperation:
    fields = [field for field in ("mount", "command_batch") if field in entry]
    if len(fields) != 1:
        raise DoodadError(
            f"trace entry {entry['sequence']} must have one renderer operation"
        )
    kind = fields[0]
    artifact = entry[kind]
    payload = _read_artifact(bundle.directory, artifact)
    return ReplayOperation(
        sequence=int(entry["sequence"]),
        kind=kind,
        outcome=str(entry["outcome"]),
        scene_revision=int(entry["scene_revision"]),
        route_generation=int(entry["route_generation"]),
        scenario_time_ms=int(entry["scenario_time_ms"]),
        payload=payload,
        payload_sha256=artifact["sha256"],
        after_snapshot_sha256=entry["after_snapshot_sha256"],
    )


def _find_snapshot_artifact(
    entries: list[dict[str, Any]],
    snapshot_sha256: str,
) -> dict[str, Any]:
    for entry in reversed(entries):
        artifact = entry.get("snapshot")
        if artifact is not None and artifact["sha256"] == snapshot_sha256:
            return artifact
    raise DoodadError(
        f"trace does not contain SceneSnapshot artifact {snapshot_sha256}"
    )


def _resolve_checkpoint(
    bundle: TraceBundle,
    target_entry: dict[str, Any],
) -> dict[str, Any] | None:
    matches = [
        checkpoint
        for checkpoint in bundle.checkpoints["checkpoints"]
        if (
            checkpoint["after_revision"] == target_entry["scene_revision"]
            and checkpoint["snapshot_sha256"]
            == target_entry["after_snapshot_sha256"]
        )
    ]
    if len(matches) > 1:
        raise DoodadError(
            f"trace revision {target_entry['scene_revision']} has "
            "ambiguous checkpoints"
        )
    if not matches:
        return None
    checkpoint = matches[0]
    for field in (
        "route_generation",
        "scenario_time_ms",
        "screen_id",
    ):
        if checkpoint[field] != target_entry[field]:
            raise DoodadError(
                f"trace checkpoint differs from selected entry in {field}"
            )
    return checkpoint


def _read_artifact(
    directory: Path,
    artifact: dict[str, Any],
) -> bytes:
    path = (directory / artifact["path"]).resolve()
    try:
        path.relative_to(directory.resolve())
    except ValueError as error:
        raise DoodadError(f"trace artifact escapes its bundle: {path}") from error
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise DoodadError(f"cannot read trace artifact {path}: {error}") from error
    if (
        len(payload) != artifact["bytes"]
        or sha256_bytes(payload) != artifact["sha256"]
    ):
        raise DoodadError(f"stale or corrupt trace artifact: {path}")
    return payload


def _capture_phase(identifier: str) -> dict[str, object]:
    states = {
        "resting",
        "pressed",
        "selected",
        "disabled",
        "loading",
        "error",
        "mid_animation",
        "end_state",
    }
    if identifier not in states:
        raise DoodadError(
            f"capture phase {identifier!r} needs an explicit state mapping"
        )
    return {
        "id": identifier,
        "state": identifier,
        "animation_fraction_milli": 0,
    }


def _checkpoint_attestation(
    selection: PerfectRenderSelection,
    runtime: dict[str, int],
    snapshot: dict[str, Any],
    semantic_sha256: str,
    framebuffer_sha256: str,
) -> dict[str, Any]:
    checkpoint = selection.checkpoint
    if checkpoint is None:
        return {
            "present": False,
            "matched": False,
            "stage_index": None,
            "differences": [],
            "expected": None,
            "actual": None,
        }
    actual = {
        "after_revision": runtime["scene_revision"],
        "route_generation": runtime["route_generation"],
        "scenario_time_ms": runtime["scenario_time_ms"],
        "screen_id": snapshot["screen_id"],
        "snapshot_sha256": document_sha256(snapshot),
        "semantic_sha256": semantic_sha256,
        "framebuffer_rgb565_sha256": framebuffer_sha256,
        "mounted_nodes": runtime["mounted_nodes"],
        "mounted_events": runtime["mounted_events"],
    }
    expected = {key: checkpoint[key] for key in actual}
    differences = sorted(
        key for key in actual if actual[key] != expected[key]
    )
    return {
        "present": True,
        "matched": not differences,
        "stage_index": checkpoint["stage_index"],
        "differences": differences,
        "expected": expected,
        "actual": actual,
    }


def _artifact(path: str, payload: bytes) -> dict[str, Any]:
    return {
        "path": path,
        "sha256": sha256_bytes(payload),
        "bytes": len(payload),
    }


def _atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)
