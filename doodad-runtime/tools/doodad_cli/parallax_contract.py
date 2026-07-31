from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any

from .contract import DoodadError


ID_PATTERN = re.compile(r"^[a-z][a-z0-9_.-]{0,95}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")

COMPONENT_KINDS = {
    "screen",
    "column",
    "row",
    "scroll",
    "text",
    "button",
    "card",
    "progress",
    "stepper",
    "toggle",
    "keypad",
    "voice_orb",
    "live_card",
    "image",
}
CONTAINER_KINDS = {"screen", "column", "row", "scroll"}
INTERACTIVE_KINDS = {"button", "stepper", "toggle", "keypad", "voice_orb"}
EVENT_KINDS = {
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
}
ORIGINS = {"guest_appspec", "trusted_surface", "hybrid_projection"}
SEMANTIC_ROLES = {
    "screen",
    "heading",
    "text",
    "button",
    "toggle",
    "progress",
    "list",
    "list_item",
    "dialog",
    "slider",
    "group",
    "image",
}

NODE_KEYS = {
    "id",
    "parent_id",
    "kind",
    "depth",
    "child_count",
    "visible",
    "enabled",
    "props",
    "semantics",
    "actions",
}
PROP_KEYS = {
    "primary_text",
    "secondary_text",
    "variant",
    "tone",
    "size",
    "gap",
    "alignment",
    "value",
    "minimum",
    "maximum",
    "step",
    "checked",
    "keys",
    "key_columns",
    "state",
    "icon",
    "max_lines",
}
SEMANTIC_KEYS = {
    "role",
    "label",
    "value",
    "hint",
    "state_description",
}
ACTION_KEYS = {"kind", "action_id"}

KIND_PROPS: dict[str, tuple[set[str], set[str]]] = {
    "screen": ({"gap", "alignment"}, set()),
    "column": ({"gap", "alignment"}, set()),
    "row": ({"gap", "alignment"}, set()),
    "scroll": ({"gap", "alignment"}, set()),
    "text": (
        {"primary_text", "variant", "alignment", "max_lines"},
        {"primary_text", "variant", "alignment"},
    ),
    "button": (
        {"primary_text", "variant", "tone", "size", "icon"},
        {"primary_text", "variant", "tone", "size"},
    ),
    "card": (
        {"primary_text", "secondary_text", "tone"},
        {"primary_text", "secondary_text", "tone"},
    ),
    "progress": (
        {"primary_text", "value", "maximum", "variant", "tone"},
        {"value", "maximum", "variant", "tone"},
    ),
    "stepper": (
        {
            "primary_text",
            "secondary_text",
            "value",
            "minimum",
            "maximum",
            "step",
        },
        {"primary_text", "value", "minimum", "maximum", "step"},
    ),
    "toggle": (
        {"primary_text", "checked", "tone"},
        {"primary_text", "checked", "tone"},
    ),
    "keypad": (
        {"keys", "key_columns"},
        {"keys", "key_columns"},
    ),
    "voice_orb": (
        {"primary_text", "secondary_text", "state", "tone"},
        {"primary_text", "state", "tone"},
    ),
    "live_card": (
        {"primary_text", "secondary_text", "value", "maximum", "tone"},
        {"primary_text", "secondary_text", "tone"},
    ),
    "image": (
        {"primary_text", "variant"},
        {"primary_text", "variant"},
    ),
}


def canonical_json_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def document_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_json_bytes(value)).hexdigest()


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DoodadError(f"{path} must be an object")
    return value


def _array(value: Any, path: str, *, minimum: int, maximum: int) -> list[Any]:
    if not isinstance(value, list) or not minimum <= len(value) <= maximum:
        raise DoodadError(
            f"{path} must contain {minimum}..{maximum} entries"
        )
    return value


def _exact_keys(
    value: dict[str, Any],
    path: str,
    *,
    required: set[str],
    allowed: set[str],
) -> None:
    missing = required - set(value)
    if missing:
        raise DoodadError(f"{path} is missing fields: {sorted(missing)}")
    unknown = set(value) - allowed
    if unknown:
        raise DoodadError(f"{path} contains unknown fields: {sorted(unknown)}")


def _identifier(value: Any, path: str) -> str:
    if not isinstance(value, str) or ID_PATTERN.fullmatch(value) is None:
        raise DoodadError(f"{path} must be a lowercase identifier")
    return value


def _bounded_string(
    value: Any,
    path: str,
    maximum: int,
    *,
    allow_empty: bool = False,
) -> str:
    if (
        not isinstance(value, str)
        or (not allow_empty and not value)
        or len(value) > maximum
    ):
        minimum = 0 if allow_empty else 1
        raise DoodadError(
            f"{path} must contain {minimum}..{maximum} characters"
        )
    return value


def _unsigned(value: Any, path: str, maximum: int = (1 << 63) - 1) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or not 0 <= value <= maximum
    ):
        raise DoodadError(f"{path} must be an unsigned integer")
    return value


def _sha256(value: Any, path: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise DoodadError(f"{path} must be a lowercase SHA-256 digest")
    return value


def _relative_path(value: Any, path: str) -> str:
    text = _bounded_string(value, path, 256)
    candidate = Path(text)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise DoodadError(f"{path} must be a safe relative path")
    return text


def _validate_action(value: Any, path: str) -> None:
    action = _object(value, path)
    _exact_keys(
        action,
        path,
        required=ACTION_KEYS,
        allowed=ACTION_KEYS,
    )
    if action["kind"] not in EVENT_KINDS:
        raise DoodadError(f"{path}.kind is unsupported")
    _identifier(action["action_id"], f"{path}.action_id")


def _validate_semantics(value: Any, path: str) -> None:
    semantics = _object(value, path)
    _exact_keys(
        semantics,
        path,
        required={"role", "label"},
        allowed=SEMANTIC_KEYS,
    )
    if semantics["role"] not in SEMANTIC_ROLES:
        raise DoodadError(f"{path}.role is unsupported")
    _bounded_string(
        semantics["label"],
        f"{path}.label",
        128,
        allow_empty=True,
    )
    for key in ("value", "hint", "state_description"):
        if key in semantics:
            _bounded_string(
                semantics[key],
                f"{path}.{key}",
                128,
                allow_empty=True,
            )


def _validate_props(kind: str, value: Any, path: str) -> None:
    props = _object(value, path)
    allowed, required = KIND_PROPS[kind]
    _exact_keys(
        props,
        path,
        required=required,
        allowed=allowed,
    )
    for key in (
        "primary_text",
        "secondary_text",
        "variant",
        "tone",
        "size",
        "gap",
        "alignment",
        "state",
        "icon",
    ):
        if key in props:
            _bounded_string(
                props[key],
                f"{path}.{key}",
                256,
                allow_empty=key in {"primary_text", "secondary_text"},
            )
    for key in ("value", "minimum", "maximum", "step"):
        if key in props and (
            isinstance(props[key], bool) or not isinstance(props[key], int)
        ):
            raise DoodadError(f"{path}.{key} must be an integer")
    if "checked" in props and not isinstance(props["checked"], bool):
        raise DoodadError(f"{path}.checked must be a boolean")
    if "max_lines" in props:
        maximum_lines = props["max_lines"]
        if (
            isinstance(maximum_lines, bool)
            or not isinstance(maximum_lines, int)
            or not 1 <= maximum_lines <= 4
        ):
            raise DoodadError(f"{path}.max_lines must be in 1..4")
    if "keys" in props:
        keys = _array(
            props["keys"],
            f"{path}.keys",
            minimum=1,
            maximum=20,
        )
        for index, key in enumerate(keys):
            _bounded_string(key, f"{path}.keys[{index}]", 4)
    if "key_columns" in props:
        columns = props["key_columns"]
        if (
            isinstance(columns, bool)
            or not isinstance(columns, int)
            or not 2 <= columns <= 5
        ):
            raise DoodadError(f"{path}.key_columns must be in 2..5")
    if kind == "progress":
        value_int = props["value"]
        maximum = props["maximum"]
        if maximum <= 0 or not 0 <= value_int <= maximum:
            raise DoodadError(f"{path} has an invalid progress range")
    if kind == "stepper":
        minimum = props["minimum"]
        maximum = props["maximum"]
        value_int = props["value"]
        step = props["step"]
        if step <= 0 or minimum > maximum or not minimum <= value_int <= maximum:
            raise DoodadError(f"{path} has an invalid stepper range")
    if kind == "live_card" and ("value" in props) != ("maximum" in props):
        raise DoodadError(
            f"{path}.value and maximum must appear together"
        )
    if kind == "image":
        _sha256(props["primary_text"], f"{path}.primary_text")
        if props["variant"] not in {"cover", "contain"}:
            raise DoodadError(f"{path}.variant is unsupported")


def validate_scene_snapshot(document: dict[str, Any]) -> None:
    _exact_keys(
        document,
        "snapshot",
        required={
            "schema_version",
            "app_id",
            "screen_id",
            "origin",
            "nodes",
        },
        allowed={
            "schema_version",
            "app_id",
            "screen_id",
            "origin",
            "nodes",
        },
    )
    if document["schema_version"] != 1:
        raise DoodadError("snapshot.schema_version must be 1")
    _identifier(document["app_id"], "snapshot.app_id")
    screen_id = _identifier(document["screen_id"], "snapshot.screen_id")
    if document["origin"] not in ORIGINS:
        raise DoodadError("snapshot.origin is unsupported")

    nodes = _array(
        document["nodes"],
        "snapshot.nodes",
        minimum=1,
        maximum=250,
    )
    ids: dict[str, tuple[int, int, str]] = {}
    child_counts: dict[str, int] = {}
    scrolls = 0
    for index, node_value in enumerate(nodes):
        path = f"snapshot.nodes[{index}]"
        node = _object(node_value, path)
        _exact_keys(
            node,
            path,
            required=NODE_KEYS,
            allowed=NODE_KEYS,
        )
        node_id = _identifier(node["id"], f"{path}.id")
        if node_id in ids:
            raise DoodadError(f"{path}.id duplicates {node_id!r}")
        kind = node["kind"]
        if kind not in COMPONENT_KINDS:
            raise DoodadError(f"{path}.kind is unsupported")
        depth = _unsigned(node["depth"], f"{path}.depth", 12)
        _unsigned(node["child_count"], f"{path}.child_count", 32)
        if not isinstance(node["visible"], bool):
            raise DoodadError(f"{path}.visible must be a boolean")
        if not isinstance(node["enabled"], bool):
            raise DoodadError(f"{path}.enabled must be a boolean")

        parent_id = node["parent_id"]
        if index == 0:
            if (
                parent_id is not None
                or depth != 0
                or kind != "screen"
                or node_id != screen_id
            ):
                raise DoodadError(
                    "snapshot root must be the declared screen"
                )
        else:
            parent = _identifier(parent_id, f"{path}.parent_id")
            if parent not in ids:
                raise DoodadError(f"{path}.parent_id must precede the node")
            parent_index, parent_depth, parent_kind = ids[parent]
            if parent_index >= index or parent_kind not in CONTAINER_KINDS:
                raise DoodadError(f"{path}.parent_id is not a container")
            if depth != parent_depth + 1:
                raise DoodadError(f"{path}.depth does not match its parent")
            child_counts[parent] = child_counts.get(parent, 0) + 1

        if kind == "scroll":
            scrolls += 1
            if scrolls > 1:
                raise DoodadError("snapshot contains multiple scroll axes")
        _validate_props(kind, node["props"], f"{path}.props")
        _validate_semantics(node["semantics"], f"{path}.semantics")
        actions = _array(
            node["actions"],
            f"{path}.actions",
            minimum=0,
            maximum=16,
        )
        action_pairs: set[tuple[str, str]] = set()
        for action_index, action in enumerate(actions):
            _validate_action(
                action,
                f"{path}.actions[{action_index}]",
            )
            pair = (action["kind"], action["action_id"])
            if pair in action_pairs:
                raise DoodadError(f"{path}.actions contains a duplicate")
            action_pairs.add(pair)
        if kind in INTERACTIVE_KINDS and (
            not actions or not node["semantics"]["label"]
        ):
            raise DoodadError(
                f"{path} is interactive but lacks semantics or actions"
            )
        if kind == "image" and not node["semantics"]["label"]:
            raise DoodadError(f"{path} image lacks a semantic label")
        ids[node_id] = (index, depth, kind)
        child_counts.setdefault(node_id, 0)

    for index, node in enumerate(nodes):
        if node["child_count"] != child_counts[node["id"]]:
            raise DoodadError(
                f"snapshot.nodes[{index}].child_count is inconsistent"
            )


def _validate_artifact(value: Any, path: str) -> None:
    artifact = _object(value, path)
    _exact_keys(
        artifact,
        path,
        required={"path", "sha256", "bytes"},
        allowed={"path", "sha256", "bytes"},
    )
    _relative_path(artifact["path"], f"{path}.path")
    _sha256(artifact["sha256"], f"{path}.sha256")
    _unsigned(artifact["bytes"], f"{path}.bytes", 16 * 1024 * 1024)


def _validate_cause(value: Any, path: str) -> None:
    cause = _object(value, path)
    allowed = {
        "kind",
        "node_id",
        "action_id",
        "event_kind",
        "provider",
        "event",
        "typed_value",
    }
    _exact_keys(cause, path, required={"kind"}, allowed=allowed)
    kinds = {
        "start",
        "semantic_action",
        "provider_event",
        "timer",
        "lifecycle",
        "restore",
    }
    if cause["kind"] not in kinds:
        raise DoodadError(f"{path}.kind is unsupported")
    for key in ("node_id", "action_id", "provider", "event"):
        if key in cause:
            _identifier(cause[key], f"{path}.{key}")
    if "event_kind" in cause and cause["event_kind"] not in EVENT_KINDS:
        raise DoodadError(f"{path}.event_kind is unsupported")
    if cause["kind"] == "semantic_action":
        required = {"node_id", "action_id", "event_kind"}
        if not required <= set(cause):
            raise DoodadError(f"{path} lacks semantic action identity")


def _validate_trace_app(value: Any, path: str) -> None:
    app = _object(value, path)
    keys = {
        "slug",
        "id",
        "package_sha256",
        "wasm_sha256",
        "manifest_sha256",
    }
    _exact_keys(app, path, required=keys, allowed=keys)
    _identifier(app["slug"], f"{path}.slug")
    _identifier(app["id"], f"{path}.id")
    for key in ("package_sha256", "wasm_sha256", "manifest_sha256"):
        _sha256(app[key], f"{path}.{key}")


def _validate_environment(value: Any, path: str) -> None:
    environment = _object(value, path)
    required = {
        "profile_id",
        "locale",
        "timezone",
        "font_scale_milli",
        "reduced_motion",
        "origin",
        "versions",
        "hashes",
    }
    _exact_keys(environment, path, required=required, allowed=required)
    _identifier(environment["profile_id"], f"{path}.profile_id")
    _bounded_string(environment["locale"], f"{path}.locale", 32)
    _bounded_string(environment["timezone"], f"{path}.timezone", 64)
    scale = _unsigned(
        environment["font_scale_milli"],
        f"{path}.font_scale_milli",
        2000,
    )
    if scale < 500:
        raise DoodadError(f"{path}.font_scale_milli must be in 500..2000")
    if not isinstance(environment["reduced_motion"], bool):
        raise DoodadError(f"{path}.reduced_motion must be a boolean")
    if environment["origin"] not in ORIGINS:
        raise DoodadError(f"{path}.origin is unsupported")

    versions = _object(environment["versions"], f"{path}.versions")
    version_keys = {
        "wamr",
        "lvgl",
        "host_abi",
        "appspec",
        "component_set",
        "simulator",
    }
    _exact_keys(
        versions,
        f"{path}.versions",
        required=version_keys,
        allowed=version_keys,
    )
    for key in version_keys:
        _bounded_string(
            versions[key],
            f"{path}.versions.{key}",
            64,
        )

    hashes = _object(environment["hashes"], f"{path}.hashes")
    hash_keys = {
        "interpretation_policy",
        "theme",
        "font",
        "icons",
        "simulator_build",
    }
    _exact_keys(
        hashes,
        f"{path}.hashes",
        required=hash_keys,
        allowed=hash_keys,
    )
    for key in hash_keys:
        _sha256(hashes[key], f"{path}.hashes.{key}")


def validate_scene_trace(document: dict[str, Any]) -> None:
    keys = {
        "schema_version",
        "id",
        "app",
        "environment",
        "scenario_id",
        "entries",
    }
    _exact_keys(document, "trace", required=keys, allowed=keys)
    if document["schema_version"] != 1:
        raise DoodadError("trace.schema_version must be 1")
    _identifier(document["id"], "trace.id")
    _validate_trace_app(document["app"], "trace.app")
    _validate_environment(document["environment"], "trace.environment")
    _identifier(document["scenario_id"], "trace.scenario_id")
    entries = _array(
        document["entries"],
        "trace.entries",
        minimum=1,
        maximum=4096,
    )

    prior_scene_revision = 0
    prior_snapshot: str | None = None
    prior_time = 0
    for index, entry_value in enumerate(entries):
        path = f"trace.entries[{index}]"
        entry = _object(entry_value, path)
        required = {
            "sequence",
            "scenario_time_ms",
            "cause",
            "outcome",
            "scene_revision",
            "route_generation",
            "screen_id",
            "before_snapshot_sha256",
            "after_snapshot_sha256",
        }
        allowed = required | {
            "cause_payload",
            "mount",
            "command_batch",
            "snapshot",
            "failure",
        }
        _exact_keys(entry, path, required=required, allowed=allowed)
        if _unsigned(entry["sequence"], f"{path}.sequence") != index:
            raise DoodadError(f"{path}.sequence must be contiguous")
        scenario_time = _unsigned(
            entry["scenario_time_ms"],
            f"{path}.scenario_time_ms",
        )
        if scenario_time < prior_time:
            raise DoodadError(f"{path}.scenario_time_ms moved backwards")
        prior_time = scenario_time
        _validate_cause(entry["cause"], f"{path}.cause")
        outcome = entry["outcome"]
        if outcome not in {"committed", "no_change", "rejected"}:
            raise DoodadError(f"{path}.outcome is unsupported")
        scene_revision = _unsigned(
            entry["scene_revision"],
            f"{path}.scene_revision",
            0xFFFFFFFF,
        )
        _unsigned(
            entry["route_generation"],
            f"{path}.route_generation",
            0xFFFFFFFF,
        )
        _identifier(entry["screen_id"], f"{path}.screen_id")

        before = entry["before_snapshot_sha256"]
        if before is not None:
            before = _sha256(before, f"{path}.before_snapshot_sha256")
        after = entry["after_snapshot_sha256"]
        if after is not None:
            after = _sha256(after, f"{path}.after_snapshot_sha256")
        if before != prior_snapshot:
            raise DoodadError(f"{path}.before_snapshot_sha256 is not prior state")

        for key in (
            "cause_payload",
            "mount",
            "command_batch",
            "snapshot",
        ):
            if key in entry:
                _validate_artifact(entry[key], f"{path}.{key}")
        if "snapshot" in entry and after != entry["snapshot"]["sha256"]:
            raise DoodadError(f"{path}.snapshot does not match after hash")

        if outcome == "committed":
            if scene_revision != prior_scene_revision + 1:
                raise DoodadError(f"{path} must advance the scene revision")
            if after is None or "snapshot" not in entry:
                raise DoodadError(f"{path} must record a committed snapshot")
            if "mount" not in entry and "command_batch" not in entry:
                raise DoodadError(f"{path} has no committed renderer input")
            if "failure" in entry:
                raise DoodadError(f"{path} cannot commit and fail")
            prior_scene_revision = scene_revision
            prior_snapshot = after
        elif outcome == "no_change":
            if (
                scene_revision != prior_scene_revision
                or after != prior_snapshot
                or "failure" in entry
            ):
                raise DoodadError(f"{path} changed state despite no_change")
            prior_snapshot = after
        else:
            if (
                scene_revision != prior_scene_revision
                or after != prior_snapshot
                or "failure" not in entry
            ):
                raise DoodadError(f"{path} rejected outcome is inconsistent")
            _bounded_string(entry["failure"], f"{path}.failure", 256)


def _validate_bounds(value: Any, path: str) -> None:
    bounds = _object(value, path)
    keys = {"x", "y", "width", "height"}
    _exact_keys(bounds, path, required=keys, allowed=keys)
    for key in ("x", "y"):
        if isinstance(bounds[key], bool) or not isinstance(bounds[key], int):
            raise DoodadError(f"{path}.{key} must be an integer")
    for key in ("width", "height"):
        if (
            isinstance(bounds[key], bool)
            or not isinstance(bounds[key], int)
            or bounds[key] < 0
        ):
            raise DoodadError(f"{path}.{key} must be non-negative")


def validate_node_evidence(document: dict[str, Any]) -> None:
    keys = {
        "schema_version",
        "snapshot_sha256",
        "capture_phase",
        "renderer",
        "profile_id",
        "physical_width_px",
        "physical_height_px",
        "nodes",
    }
    _exact_keys(document, "evidence", required=keys, allowed=keys)
    if document["schema_version"] != 1:
        raise DoodadError("evidence.schema_version must be 1")
    _sha256(document["snapshot_sha256"], "evidence.snapshot_sha256")
    _identifier(document["profile_id"], "evidence.profile_id")
    _unsigned(
        document["physical_width_px"],
        "evidence.physical_width_px",
        4096,
    )
    _unsigned(
        document["physical_height_px"],
        "evidence.physical_height_px",
        4096,
    )

    phase = _object(document["capture_phase"], "evidence.capture_phase")
    phase_allowed = {
        "id",
        "state",
        "animation_fraction_milli",
        "target",
        "scroll_anchor",
    }
    _exact_keys(
        phase,
        "evidence.capture_phase",
        required={"id", "state", "animation_fraction_milli"},
        allowed=phase_allowed,
    )
    _identifier(phase["id"], "evidence.capture_phase.id")
    if phase["state"] not in {
        "resting",
        "pressed",
        "selected",
        "disabled",
        "loading",
        "error",
        "mid_animation",
        "end_state",
    }:
        raise DoodadError("evidence.capture_phase.state is unsupported")
    _unsigned(
        phase["animation_fraction_milli"],
        "evidence.capture_phase.animation_fraction_milli",
        1000,
    )
    for key in ("target", "scroll_anchor"):
        if key in phase:
            _identifier(phase[key], f"evidence.capture_phase.{key}")

    renderer = _object(document["renderer"], "evidence.renderer")
    renderer_keys = {"kind", "mode", "version", "build_sha256"}
    _exact_keys(
        renderer,
        "evidence.renderer",
        required=renderer_keys,
        allowed=renderer_keys,
    )
    if renderer["kind"] not in {"compose", "lvgl"}:
        raise DoodadError("evidence.renderer.kind is unsupported")
    if renderer["mode"] not in {"host", "emulator", "simulator", "hardware"}:
        raise DoodadError("evidence.renderer.mode is unsupported")
    _bounded_string(renderer["version"], "evidence.renderer.version", 64)
    _sha256(renderer["build_sha256"], "evidence.renderer.build_sha256")

    nodes = _array(
        document["nodes"],
        "evidence.nodes",
        minimum=1,
        maximum=250,
    )
    seen: set[str] = set()
    for index, node_value in enumerate(nodes):
        path = f"evidence.nodes[{index}]"
        node = _object(node_value, path)
        required = {
            "id",
            "parent_id",
            "role",
            "label",
            "value",
            "state_description",
            "visible",
            "enabled",
            "actions",
            "bounds_px",
            "bounds_dp_q8_8",
            "token_roles",
        }
        allowed = required | {"selected", "checked", "text"}
        _exact_keys(node, path, required=required, allowed=allowed)
        node_id = _identifier(node["id"], f"{path}.id")
        if node_id in seen:
            raise DoodadError(f"{path}.id duplicates {node_id!r}")
        if node["parent_id"] is not None:
            parent = _identifier(node["parent_id"], f"{path}.parent_id")
            if parent not in seen:
                raise DoodadError(f"{path}.parent_id must precede the node")
        if node["role"] not in SEMANTIC_ROLES:
            raise DoodadError(f"{path}.role is unsupported")
        for key in ("label", "value", "state_description"):
            _bounded_string(
                node[key],
                f"{path}.{key}",
                128,
                allow_empty=True,
            )
        for key in ("visible", "enabled", "selected", "checked"):
            if key in node and not isinstance(node[key], bool):
                raise DoodadError(f"{path}.{key} must be a boolean")
        actions = _array(
            node["actions"],
            f"{path}.actions",
            minimum=0,
            maximum=16,
        )
        for action_index, action in enumerate(actions):
            _validate_action(action, f"{path}.actions[{action_index}]")
        _validate_bounds(node["bounds_px"], f"{path}.bounds_px")
        _validate_bounds(
            node["bounds_dp_q8_8"],
            f"{path}.bounds_dp_q8_8",
        )
        token_roles = _object(node["token_roles"], f"{path}.token_roles")
        for key, value in token_roles.items():
            _identifier(key, f"{path}.token_roles key")
            _identifier(value, f"{path}.token_roles.{key}")
        if "text" in node:
            text = _object(node["text"], f"{path}.text")
            text_keys = {"line_count", "truncated", "baselines_px"}
            _exact_keys(
                text,
                f"{path}.text",
                required=text_keys,
                allowed=text_keys,
            )
            _unsigned(text["line_count"], f"{path}.text.line_count", 16)
            if not isinstance(text["truncated"], bool):
                raise DoodadError(f"{path}.text.truncated must be a boolean")
            baselines = _array(
                text["baselines_px"],
                f"{path}.text.baselines_px",
                minimum=0,
                maximum=16,
            )
            for baseline_index, baseline in enumerate(baselines):
                if isinstance(baseline, bool) or not isinstance(baseline, int):
                    raise DoodadError(
                        f"{path}.text.baselines_px[{baseline_index}] "
                        "must be an integer"
                    )
        seen.add(node_id)


def validate_perfect_render_suite(document: dict[str, Any]) -> None:
    keys = {"schema_version", "id", "entries"}
    _exact_keys(document, "suite", required=keys, allowed=keys)
    if document["schema_version"] != 1:
        raise DoodadError("suite.schema_version must be 1")
    _identifier(document["id"], "suite.id")
    entries = _array(
        document["entries"],
        "suite.entries",
        minimum=1,
        maximum=4096,
    )
    seen: set[tuple[str, int, str, str]] = set()
    for index, entry_value in enumerate(entries):
        path = f"suite.entries[{index}]"
        entry = _object(entry_value, path)
        keys = {
            "app_slug",
            "trace",
            "sequence",
            "snapshot_sha256",
            "capture_phase",
            "profile_id",
            "compose",
            "lvgl",
            "comparison_policy",
            "review",
        }
        _exact_keys(entry, path, required=keys, allowed=keys)
        app_slug = _identifier(entry["app_slug"], f"{path}.app_slug")
        _relative_path(entry["trace"], f"{path}.trace")
        sequence = _unsigned(entry["sequence"], f"{path}.sequence")
        _sha256(entry["snapshot_sha256"], f"{path}.snapshot_sha256")
        capture_phase = _identifier(
            entry["capture_phase"],
            f"{path}.capture_phase",
        )
        profile = _identifier(entry["profile_id"], f"{path}.profile_id")
        for renderer_name, modes in (
            ("compose", {"host", "emulator"}),
            ("lvgl", {"simulator", "hardware"}),
        ):
            renderer = _object(entry[renderer_name], f"{path}.{renderer_name}")
            renderer_keys = {"mode", "version"}
            _exact_keys(
                renderer,
                f"{path}.{renderer_name}",
                required=renderer_keys,
                allowed=renderer_keys,
            )
            if renderer["mode"] not in modes:
                raise DoodadError(
                    f"{path}.{renderer_name}.mode is unsupported"
                )
            _bounded_string(
                renderer["version"],
                f"{path}.{renderer_name}.version",
                64,
            )
        _identifier(entry["comparison_policy"], f"{path}.comparison_policy")
        review = _object(entry["review"], f"{path}.review")
        review_required = {"status"}
        review_allowed = {"status", "reviewer", "reviewed_at", "notes"}
        _exact_keys(
            review,
            f"{path}.review",
            required=review_required,
            allowed=review_allowed,
        )
        if review["status"] not in {"pending", "approved", "rejected"}:
            raise DoodadError(f"{path}.review.status is unsupported")
        for key in ("reviewer", "reviewed_at", "notes"):
            if key in review:
                _bounded_string(
                    review[key],
                    f"{path}.review.{key}",
                    512 if key == "notes" else 128,
                )
        identity = (app_slug, sequence, capture_phase, profile)
        if identity in seen:
            raise DoodadError(f"{path} duplicates a suite entry")
        seen.add(identity)
