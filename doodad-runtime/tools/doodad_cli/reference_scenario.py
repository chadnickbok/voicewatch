from __future__ import annotations

import math
import re
from pathlib import Path
from typing import Any, Iterable

from .contract import DoodadError, read_json


ID_PATTERN = re.compile(r"^[a-z][a-z0-9_.-]{0,95}$")
SCENARIO_KEYS = {
    "schema_version",
    "id",
    "scene",
    "title",
    "appspec",
    "lifecycle_scenario",
    "data",
    "ui_state",
    "theme",
    "render_profiles",
    "font_scale",
    "interaction",
    "expected_semantics",
}
REQUIRED_SCENARIO_KEYS = SCENARIO_KEYS - {"appspec", "lifecycle_scenario"}
THEME_KEYS = {
    "color_scheme",
    "typography",
    "shapes",
    "motion_scheme",
    "dynamic_color",
    "ambient",
    "reduced_motion",
}
INTERACTION_KEYS = {"state", "target", "animation_fraction"}
INTERACTION_STATES = {
    "resting",
    "pressed",
    "selected",
    "disabled",
    "loading",
    "error",
    "mid_animation",
    "end_state",
}
SEMANTIC_KEYS = {
    "id",
    "role",
    "label",
    "value",
    "state_description",
    "enabled",
    "selected",
    "checked",
    "children",
}
SEMANTIC_ROLES = {
    "screen",
    "header",
    "text",
    "button",
    "toggle",
    "progress",
    "list",
    "list_item",
    "dialog",
    "stepper",
    "group",
}
PROFILE_KEYS = {
    "id",
    "physical_width_px",
    "physical_height_px",
    "logical_width_dp",
    "logical_height_dp",
    "density",
    "shape",
    "geometry_family",
    "input",
}
INPUT_KINDS = {"touch", "rotary", "buttons"}


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DoodadError(f"{path} must be an object")
    return value


def _identifier(value: Any, path: str) -> str:
    if not isinstance(value, str) or ID_PATTERN.fullmatch(value) is None:
        raise DoodadError(f"{path} must be a lowercase identifier")
    return value


def _bounded_string(value: Any, path: str, maximum: int) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise DoodadError(f"{path} must contain 1..{maximum} characters")
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


def _repository_file(project_root: Path, value: Any, path: str) -> None:
    relative = _bounded_string(value, path, 256)
    candidate = Path(relative)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise DoodadError(f"{path} must be a project-root-relative path")
    resolved = (project_root / candidate).resolve()
    try:
        resolved.relative_to(project_root.resolve())
    except ValueError as error:
        raise DoodadError(f"{path} escapes the project root") from error
    if not resolved.is_file():
        raise DoodadError(f"{path} does not exist: {relative}")


def _semantic_nodes(
    root: Any,
    *,
    path: str,
    seen: set[str],
) -> int:
    node = _object(root, path)
    _exact_keys(
        node,
        path,
        required={"id", "role", "label"},
        allowed=SEMANTIC_KEYS,
    )
    node_id = _identifier(node["id"], f"{path}.id")
    if node_id in seen:
        raise DoodadError(f"{path}.id duplicates semantic id {node_id!r}")
    seen.add(node_id)
    if node["role"] not in SEMANTIC_ROLES:
        raise DoodadError(f"{path}.role is unsupported")
    _bounded_string(node["label"], f"{path}.label", 128)
    for key in ("value", "state_description"):
        if key in node and (
            not isinstance(node[key], str) or len(node[key]) > 128
        ):
            raise DoodadError(f"{path}.{key} must be a string of at most 128 characters")
    for key in ("enabled", "selected", "checked"):
        if key in node and not isinstance(node[key], bool):
            raise DoodadError(f"{path}.{key} must be a boolean")
    children = node.get("children", [])
    if not isinstance(children, list) or len(children) > 64:
        raise DoodadError(f"{path}.children must contain at most 64 nodes")
    count = 1
    for index, child in enumerate(children):
        count += _semantic_nodes(
            child,
            path=f"{path}.children[{index}]",
            seen=seen,
        )
    return count


def validate_reference_scenario(
    document: dict[str, Any],
    *,
    project_root: Path,
    known_profiles: set[str],
    path: str = "scenario",
) -> None:
    _exact_keys(
        document,
        path,
        required=REQUIRED_SCENARIO_KEYS,
        allowed=SCENARIO_KEYS,
    )
    if document["schema_version"] != 1:
        raise DoodadError(f"{path}.schema_version must be 1")
    _identifier(document["id"], f"{path}.id")
    _identifier(document["scene"], f"{path}.scene")
    _bounded_string(document["title"], f"{path}.title", 64)
    if not isinstance(document["data"], dict):
        raise DoodadError(f"{path}.data must be an object")

    ui_state = _object(document["ui_state"], f"{path}.ui_state")
    _identifier(ui_state.get("status"), f"{path}.ui_state.status")

    theme = _object(document["theme"], f"{path}.theme")
    _exact_keys(
        theme,
        f"{path}.theme",
        required=THEME_KEYS,
        allowed=THEME_KEYS,
    )
    for key in ("color_scheme", "typography", "shapes"):
        _identifier(theme[key], f"{path}.theme.{key}")
    if theme["motion_scheme"] not in {"expressive", "standard"}:
        raise DoodadError(f"{path}.theme.motion_scheme is unsupported")
    for key in ("dynamic_color", "ambient", "reduced_motion"):
        if not isinstance(theme[key], bool):
            raise DoodadError(f"{path}.theme.{key} must be a boolean")

    profiles = document["render_profiles"]
    if (
        not isinstance(profiles, list)
        or not 1 <= len(profiles) <= 8
        or not all(isinstance(profile, str) for profile in profiles)
        or len(set(profiles)) != len(profiles)
    ):
        raise DoodadError(f"{path}.render_profiles must be a unique non-empty array")
    unknown_profiles = set(profiles) - known_profiles
    if unknown_profiles:
        raise DoodadError(
            f"{path}.render_profiles contains unknown profiles: "
            f"{sorted(unknown_profiles)}"
        )

    font_scale = document["font_scale"]
    if (
        isinstance(font_scale, bool)
        or not isinstance(font_scale, (int, float))
        or not math.isfinite(float(font_scale))
        or not 0.85 <= float(font_scale) <= 1.3
    ):
        raise DoodadError(f"{path}.font_scale must be in 0.85..1.3")

    interaction = _object(document["interaction"], f"{path}.interaction")
    _exact_keys(
        interaction,
        f"{path}.interaction",
        required={"state", "animation_fraction"},
        allowed=INTERACTION_KEYS,
    )
    if interaction["state"] not in INTERACTION_STATES:
        raise DoodadError(f"{path}.interaction.state is unsupported")
    if "target" in interaction:
        _identifier(interaction["target"], f"{path}.interaction.target")
    fraction = interaction["animation_fraction"]
    if (
        isinstance(fraction, bool)
        or not isinstance(fraction, (int, float))
        or not math.isfinite(float(fraction))
        or not 0 <= float(fraction) <= 1
    ):
        raise DoodadError(f"{path}.interaction.animation_fraction must be in 0..1")

    if "appspec" in document:
        _repository_file(project_root, document["appspec"], f"{path}.appspec")
    if "lifecycle_scenario" in document:
        _repository_file(
            project_root,
            document["lifecycle_scenario"],
            f"{path}.lifecycle_scenario",
        )

    node_count = _semantic_nodes(
        document["expected_semantics"],
        path=f"{path}.expected_semantics",
        seen=set(),
    )
    if node_count > 128:
        raise DoodadError(f"{path}.expected_semantics exceeds 128 nodes")


def load_reference_scenarios(
    project_root: Path,
) -> list[dict[str, Any]]:
    reference_root = project_root / "reference"
    profiles_document = read_json(reference_root / "display-profiles.json")
    profiles = profiles_document.get("profiles")
    if profiles_document.get("schema_version") != 1 or not isinstance(profiles, list):
        raise DoodadError("reference/display-profiles.json is invalid")
    profile_ids: set[str] = set()
    for index, profile_value in enumerate(profiles):
        profile_path = f"display-profiles.profiles[{index}]"
        profile = _object(
            profile_value,
            profile_path,
        )
        _exact_keys(
            profile,
            profile_path,
            required=PROFILE_KEYS,
            allowed=PROFILE_KEYS,
        )
        profile_id = _identifier(
            profile.get("id"),
            f"{profile_path}.id",
        )
        if profile_id in profile_ids:
            raise DoodadError(f"duplicate display profile {profile_id!r}")
        profile_ids.add(profile_id)
        for key in (
            "physical_width_px",
            "physical_height_px",
            "logical_width_dp",
            "logical_height_dp",
        ):
            value = profile[key]
            if (
                isinstance(value, bool)
                or not isinstance(value, int)
                or not 1 <= value <= 4096
            ):
                raise DoodadError(f"{profile_path}.{key} must be in 1..4096")
        density = profile["density"]
        if (
            isinstance(density, bool)
            or not isinstance(density, (int, float))
            or not math.isfinite(float(density))
            or not 0.5 <= float(density) <= 8
        ):
            raise DoodadError(f"{profile_path}.density must be in 0.5..8")
        for physical_key, logical_key in (
            ("physical_width_px", "logical_width_dp"),
            ("physical_height_px", "logical_height_dp"),
        ):
            expected = float(profile[logical_key]) * float(density)
            if abs(float(profile[physical_key]) - expected) > 2:
                raise DoodadError(
                    f"{profile_path}.{physical_key} does not match "
                    f"{logical_key} × density"
                )
        if profile["shape"] not in {"round", "square"}:
            raise DoodadError(f"{profile_path}.shape is unsupported")
        _identifier(
            profile["geometry_family"],
            f"{profile_path}.geometry_family",
        )
        inputs = profile["input"]
        if (
            not isinstance(inputs, list)
            or not inputs
            or not all(isinstance(item, str) for item in inputs)
            or len(inputs) != len(set(inputs))
            or not set(inputs) <= INPUT_KINDS
        ):
            raise DoodadError(
                f"{profile_path}.input must be a unique array of known inputs"
            )

    scenario_root = reference_root / "scenarios"
    index = read_json(scenario_root / "index.json")
    if set(index) != {"schema_version", "scenarios"} or index["schema_version"] != 1:
        raise DoodadError("reference/scenarios/index.json is invalid")
    filenames = index["scenarios"]
    if (
        not isinstance(filenames, list)
        or not filenames
        or not all(isinstance(filename, str) for filename in filenames)
        or len(set(filenames)) != len(filenames)
    ):
        raise DoodadError("reference scenario index must be a unique non-empty array")

    documents: list[dict[str, Any]] = []
    ids: set[str] = set()
    for filename in filenames:
        relative = Path(filename)
        if relative.is_absolute() or len(relative.parts) != 1 or relative.suffix != ".json":
            raise DoodadError(f"invalid reference scenario filename: {filename!r}")
        document = read_json(scenario_root / relative)
        validate_reference_scenario(
            document,
            project_root=project_root,
            known_profiles=profile_ids,
            path=f"reference/scenarios/{filename}",
        )
        scenario_id = str(document["id"])
        if scenario_id in ids:
            raise DoodadError(f"duplicate reference scenario id {scenario_id!r}")
        ids.add(scenario_id)
        documents.append(document)

    indexed_files = {scenario_root / filename for filename in filenames}
    unindexed = {
        path
        for path in scenario_root.glob("*.json")
        if path.name != "index.json" and path not in indexed_files
    }
    if unindexed:
        names = sorted(path.name for path in unindexed)
        raise DoodadError(f"unindexed reference scenarios: {names}")
    return documents


def flatten_semantics(root: dict[str, Any]) -> Iterable[dict[str, Any]]:
    yield root
    for child in root.get("children", []):
        yield from flatten_semantics(child)
