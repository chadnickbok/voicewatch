from __future__ import annotations

import re
from copy import deepcopy
from dataclasses import dataclass
from typing import Any

from .contract import DoodadError


IDENTIFIER = re.compile(r"^[a-z][a-z0-9_.-]{0,95}$")
APP_STATES = {"stopped", "foreground", "background", "suspended", "crashed"}
DISPLAY_STATES = {"awake", "asleep"}
CONNECTIVITY_STATES = {"online", "degraded", "offline"}
FRESHNESS_STATES = {"current", "stale", "offline", "error"}
SURFACE_NAMES = {
    "app",
    "glance",
    "complication",
    "notification",
    "ongoing",
    "voice",
}
OPERATIONS = {
    "clock.advance",
    "clock.set_wall",
    "lifecycle.set",
    "system.reboot",
    "provider.emit",
    "surface.publish",
    "action.dispatch",
    "assert.state",
}
UINT64_MAX = (1 << 64) - 1
INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1


def _object(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DoodadError(f"{path} must be an object")
    return value


def _exact_fields(
    value: dict[str, Any],
    path: str,
    *,
    required: set[str],
    optional: set[str] | None = None,
) -> None:
    allowed = required | (optional or set())
    missing = required - set(value)
    if missing:
        raise DoodadError(f"{path} is missing fields: {sorted(missing)}")
    unknown = set(value) - allowed
    if unknown:
        raise DoodadError(f"{path} contains unknown fields: {sorted(unknown)}")


def _integer(value: Any, path: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise DoodadError(f"{path} must be an integer")
    if not minimum <= value <= maximum:
        raise DoodadError(f"{path} must be in {minimum}..{maximum}")
    return value


def _number(value: Any, path: str, minimum: float, maximum: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise DoodadError(f"{path} must be a number")
    if not minimum <= value <= maximum:
        raise DoodadError(f"{path} must be in {minimum}..{maximum}")
    return float(value)


def _text(value: Any, path: str, minimum: int, maximum: int) -> str:
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        raise DoodadError(f"{path} must contain {minimum}..{maximum} characters")
    return value


def _identifier(value: Any, path: str) -> str:
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise DoodadError(f"{path} must be a lowercase Doodad identifier")
    return value


def _enum(value: Any, path: str, choices: set[str]) -> str:
    if not isinstance(value, str) or value not in choices:
        raise DoodadError(f"{path} must be one of {sorted(choices)}")
    return value


def _validate_action(value: Any, path: str) -> None:
    action = _object(value, path)
    _exact_fields(
        action,
        path,
        required={"id", "label"},
        optional={"destructive"},
    )
    _identifier(action["id"], f"{path}.id")
    _text(action["label"], f"{path}.label", 1, 128)
    if "destructive" in action and not isinstance(action["destructive"], bool):
        raise DoodadError(f"{path}.destructive must be a boolean")


def _validate_actions(
    value: Any, path: str, *, maximum: int, minimum: int = 0
) -> None:
    if not isinstance(value, list) or not minimum <= len(value) <= maximum:
        raise DoodadError(f"{path} must contain {minimum}..{maximum} actions")
    identifiers: set[str] = set()
    for index, action in enumerate(value):
        _validate_action(action, f"{path}[{index}]")
        action_id = action["id"]
        if action_id in identifiers:
            raise DoodadError(f"{path} contains duplicate action {action_id!r}")
        identifiers.add(action_id)


def _validate_projection(
    name: str, value: Any, path: str, domain_revision: int
) -> None:
    projection = _object(value, path)
    common_revision = projection.get("revision")
    _integer(common_revision, f"{path}.revision", 0, UINT64_MAX)
    if common_revision != domain_revision:
        raise DoodadError(
            f"{path}.revision {common_revision} does not match "
            f"domain_revision {domain_revision}"
        )

    if name == "app":
        _exact_fields(
            projection,
            path,
            required={"revision", "screen_id", "title", "summary"},
            optional={"state"},
        )
        _identifier(projection["screen_id"], f"{path}.screen_id")
        _text(projection["title"], f"{path}.title", 1, 128)
        _text(projection["summary"], f"{path}.summary", 0, 256)
        if "state" in projection:
            _object(projection["state"], f"{path}.state")
        return

    if name == "glance":
        _exact_fields(
            projection,
            path,
            required={"revision", "template", "title", "primary"},
            optional={"secondary", "progress", "action"},
        )
        _enum(
            projection["template"],
            f"{path}.template",
            {"metric", "ongoing", "alert", "list"},
        )
        _text(projection["title"], f"{path}.title", 1, 128)
        _text(projection["primary"], f"{path}.primary", 1, 128)
        if "secondary" in projection:
            _text(projection["secondary"], f"{path}.secondary", 0, 128)
        if "progress" in projection:
            _number(projection["progress"], f"{path}.progress", 0, 1)
        if "action" in projection:
            _validate_action(projection["action"], f"{path}.action")
        return

    if name == "complication":
        _exact_fields(
            projection,
            path,
            required={"revision", "label", "value"},
            optional={"icon"},
        )
        _text(projection["label"], f"{path}.label", 1, 128)
        _text(projection["value"], f"{path}.value", 1, 128)
        if "icon" in projection:
            _identifier(projection["icon"], f"{path}.icon")
        return

    if name == "notification":
        _exact_fields(
            projection,
            path,
            required={"revision", "status"},
            optional={"title", "body", "privacy", "actions"},
        )
        status = _enum(
            projection["status"], f"{path}.status", {"inactive", "active"}
        )
        if status == "active":
            for field in ("title", "body", "privacy"):
                if field not in projection:
                    raise DoodadError(
                        f"{path} active notification is missing {field!r}"
                    )
        if "title" in projection:
            _text(projection["title"], f"{path}.title", 1, 128)
        if "body" in projection:
            _text(projection["body"], f"{path}.body", 1, 512)
        if "privacy" in projection:
            _enum(
                projection["privacy"],
                f"{path}.privacy",
                {"public", "private", "secret"},
            )
        if "actions" in projection:
            _validate_actions(projection["actions"], f"{path}.actions", maximum=2)
        return

    if name == "ongoing":
        _exact_fields(
            projection,
            path,
            required={"revision", "status"},
            optional={"title", "detail", "progress", "actions"},
        )
        status = _enum(
            projection["status"], f"{path}.status", {"inactive", "active"}
        )
        if status == "active" and "title" not in projection:
            raise DoodadError(f"{path} active ongoing surface is missing 'title'")
        if "title" in projection:
            _text(projection["title"], f"{path}.title", 1, 128)
        if "detail" in projection:
            _text(projection["detail"], f"{path}.detail", 0, 128)
        if "progress" in projection:
            _number(projection["progress"], f"{path}.progress", 0, 1)
        if "actions" in projection:
            _validate_actions(projection["actions"], f"{path}.actions", maximum=2)
        return

    if name == "voice":
        _exact_fields(projection, path, required={"revision", "actions"})
        actions = projection["actions"]
        if not isinstance(actions, list) or not 1 <= len(actions) <= 16:
            raise DoodadError(f"{path}.actions must contain 1..16 voice actions")
        identifiers: set[str] = set()
        for index, value in enumerate(actions):
            action_path = f"{path}.actions[{index}]"
            action = _object(value, action_path)
            _exact_fields(
                action,
                action_path,
                required={"id", "example", "confirmation"},
            )
            action_id = _identifier(action["id"], f"{action_path}.id")
            if action_id in identifiers:
                raise DoodadError(
                    f"{path}.actions contains duplicate action {action_id!r}"
                )
            identifiers.add(action_id)
            _text(action["example"], f"{action_path}.example", 1, 128)
            _enum(
                action["confirmation"],
                f"{action_path}.confirmation",
                {"never", "destructive", "always"},
            )
        return

    raise DoodadError(f"{path} uses unsupported surface {name!r}")


def validate_surface_state(document: dict[str, Any]) -> None:
    path = "surface state"
    _exact_fields(
        document,
        path,
        required={
            "schema_version",
            "app_id",
            "domain_revision",
            "observed_at_ms",
            "freshness",
            "declared_surfaces",
            "surfaces",
        },
    )
    if document["schema_version"] != 1:
        raise DoodadError("surface state schema_version must be 1")
    _identifier(document["app_id"], f"{path}.app_id")
    domain_revision = _integer(
        document["domain_revision"],
        f"{path}.domain_revision",
        0,
        UINT64_MAX,
    )
    _integer(document["observed_at_ms"], f"{path}.observed_at_ms", 0, UINT64_MAX)
    _enum(document["freshness"], f"{path}.freshness", FRESHNESS_STATES)

    declared = document["declared_surfaces"]
    if not isinstance(declared, list) or not declared:
        raise DoodadError(f"{path}.declared_surfaces must be a non-empty array")
    if not all(isinstance(name, str) for name in declared):
        raise DoodadError(f"{path}.declared_surfaces must contain strings")
    declared_set = set(declared)
    if len(declared_set) != len(declared):
        raise DoodadError(f"{path}.declared_surfaces must be unique")
    unknown = declared_set - SURFACE_NAMES
    if unknown:
        raise DoodadError(f"{path} declares unsupported surfaces: {sorted(unknown)}")

    surfaces = _object(document["surfaces"], f"{path}.surfaces")
    if set(surfaces) != declared_set:
        missing = declared_set - set(surfaces)
        extra = set(surfaces) - declared_set
        raise DoodadError(
            f"{path}.surfaces must exactly cover declared surfaces "
            f"(missing={sorted(missing)}, extra={sorted(extra)})"
        )
    for name in declared:
        _validate_projection(
            name,
            surfaces[name],
            f"{path}.surfaces.{name}",
            domain_revision,
        )


def _validate_initial(value: Any) -> None:
    initial = _object(value, "scenario.initial_state")
    _exact_fields(
        initial,
        "scenario.initial_state",
        required={
            "wall_time_ms",
            "timezone_offset_minutes",
            "app_state",
            "display_state",
            "connectivity",
        },
    )
    _integer(
        initial["wall_time_ms"],
        "scenario.initial_state.wall_time_ms",
        INT64_MIN,
        INT64_MAX,
    )
    _integer(
        initial["timezone_offset_minutes"],
        "scenario.initial_state.timezone_offset_minutes",
        -840,
        840,
    )
    _enum(initial["app_state"], "scenario.initial_state.app_state", APP_STATES)
    _enum(
        initial["display_state"],
        "scenario.initial_state.display_state",
        DISPLAY_STATES,
    )
    _enum(
        initial["connectivity"],
        "scenario.initial_state.connectivity",
        CONNECTIVITY_STATES,
    )


def _validate_step(value: Any, index: int) -> None:
    path = f"scenario.steps[{index}]"
    step = _object(value, path)
    operation = step.get("op")
    if operation not in OPERATIONS:
        raise DoodadError(f"{path}.op must be one of {sorted(OPERATIONS)}")

    if operation == "clock.advance":
        _exact_fields(step, path, required={"op", "milliseconds"})
        _integer(step["milliseconds"], f"{path}.milliseconds", 0, UINT64_MAX)
    elif operation == "clock.set_wall":
        _exact_fields(step, path, required={"op", "wall_time_ms"})
        _integer(step["wall_time_ms"], f"{path}.wall_time_ms", INT64_MIN, INT64_MAX)
    elif operation == "lifecycle.set":
        _exact_fields(
            step,
            path,
            required={"op"},
            optional={"app_state", "display_state", "connectivity"},
        )
        changed = set(step) - {"op"}
        if not changed:
            raise DoodadError(f"{path} must change at least one lifecycle field")
        if "app_state" in step:
            _enum(step["app_state"], f"{path}.app_state", APP_STATES)
        if "display_state" in step:
            _enum(step["display_state"], f"{path}.display_state", DISPLAY_STATES)
        if "connectivity" in step:
            _enum(
                step["connectivity"],
                f"{path}.connectivity",
                CONNECTIVITY_STATES,
            )
    elif operation == "system.reboot":
        _exact_fields(step, path, required={"op", "downtime_ms"})
        _integer(step["downtime_ms"], f"{path}.downtime_ms", 0, UINT64_MAX)
    elif operation == "provider.emit":
        _exact_fields(
            step,
            path,
            required={
                "op",
                "provider",
                "event",
                "revision",
                "status",
                "payload",
            },
        )
        _identifier(step["provider"], f"{path}.provider")
        _identifier(step["event"], f"{path}.event")
        _integer(step["revision"], f"{path}.revision", 0, UINT64_MAX)
        _enum(step["status"], f"{path}.status", FRESHNESS_STATES)
        _object(step["payload"], f"{path}.payload")
    elif operation == "surface.publish":
        _exact_fields(step, path, required={"op", "snapshot"})
        validate_surface_state(_object(step["snapshot"], f"{path}.snapshot"))
    elif operation == "action.dispatch":
        _exact_fields(step, path, required={"op", "target"}, optional={"value"})
        _identifier(step["target"], f"{path}.target")
    elif operation == "assert.state":
        _exact_fields(step, path, required={"op", "equals"})
        expected = _object(step["equals"], f"{path}.equals")
        if not expected:
            raise DoodadError(f"{path}.equals must not be empty")
        for state_path in expected:
            if (
                not isinstance(state_path, str)
                or not state_path
                or state_path.startswith(".")
                or state_path.endswith(".")
            ):
                raise DoodadError(
                    f"{path}.equals keys must be dotted state paths"
                )


def validate_scenario(document: dict[str, Any]) -> None:
    _exact_fields(
        document,
        "scenario",
        required={"schema_version", "id", "app_id", "initial_state", "steps"},
    )
    if document["schema_version"] != 1:
        raise DoodadError("scenario schema_version must be 1")
    _identifier(document["id"], "scenario.id")
    _identifier(document["app_id"], "scenario.app_id")
    _validate_initial(document["initial_state"])
    steps = document["steps"]
    if not isinstance(steps, list) or not 1 <= len(steps) <= 512:
        raise DoodadError("scenario.steps must contain 1..512 steps")
    for index, step in enumerate(steps):
        _validate_step(step, index)


@dataclass(frozen=True)
class ScenarioResult:
    scenario_id: str
    steps_executed: int
    state: dict[str, Any]
    trace: tuple[dict[str, Any], ...]


class ScenarioRunner:
    _TRANSITIONS = {
        "stopped": {"stopped", "foreground"},
        "foreground": {
            "foreground",
            "background",
            "suspended",
            "stopped",
            "crashed",
        },
        "background": {
            "background",
            "foreground",
            "suspended",
            "stopped",
            "crashed",
        },
        "suspended": {
            "suspended",
            "foreground",
            "background",
            "stopped",
            "crashed",
        },
        "crashed": {"crashed", "stopped"},
    }

    def run(self, document: dict[str, Any]) -> ScenarioResult:
        validate_scenario(document)
        initial = document["initial_state"]
        state: dict[str, Any] = {
            "clock": {
                "scenario_ms": 0,
                "uptime_ms": 0,
                "wall_time_ms": initial["wall_time_ms"],
                "timezone_offset_minutes": initial["timezone_offset_minutes"],
                "boot_generation": 1,
            },
            "lifecycle": {
                "app_state": initial["app_state"],
                "display_state": initial["display_state"],
                "connectivity": initial["connectivity"],
            },
            "providers": {},
            "surfaces": {},
            "actions": {"count": 0, "last_target": None, "last_value": None},
        }
        trace: list[dict[str, Any]] = []
        for index, step in enumerate(document["steps"]):
            self._execute(document["app_id"], state, step, index)
            trace.append(
                {
                    "step": index,
                    "op": step["op"],
                    "scenario_ms": state["clock"]["scenario_ms"],
                    "boot_generation": state["clock"]["boot_generation"],
                    "app_state": state["lifecycle"]["app_state"],
                }
            )
        return ScenarioResult(
            scenario_id=document["id"],
            steps_executed=len(document["steps"]),
            state=deepcopy(state),
            trace=tuple(trace),
        )

    def _advance(
        self, state: dict[str, Any], milliseconds: int, *, uptime: bool
    ) -> None:
        clock = state["clock"]
        if clock["scenario_ms"] + milliseconds > UINT64_MAX:
            raise DoodadError("scenario clock overflow")
        if uptime and clock["uptime_ms"] + milliseconds > UINT64_MAX:
            raise DoodadError("uptime clock overflow")
        if clock["wall_time_ms"] + milliseconds > INT64_MAX:
            raise DoodadError("wall clock overflow")
        clock["scenario_ms"] += milliseconds
        if uptime:
            clock["uptime_ms"] += milliseconds
        clock["wall_time_ms"] += milliseconds

    def _execute(
        self,
        app_id: str,
        state: dict[str, Any],
        step: dict[str, Any],
        index: int,
    ) -> None:
        operation = step["op"]
        if operation == "clock.advance":
            self._advance(state, step["milliseconds"], uptime=True)
            return
        if operation == "clock.set_wall":
            state["clock"]["wall_time_ms"] = step["wall_time_ms"]
            return
        if operation == "lifecycle.set":
            lifecycle = state["lifecycle"]
            if "app_state" in step:
                before = lifecycle["app_state"]
                after = step["app_state"]
                if after not in self._TRANSITIONS[before]:
                    raise DoodadError(
                        f"scenario.steps[{index}] cannot transition app "
                        f"from {before} to {after}"
                    )
                lifecycle["app_state"] = after
            for field in ("display_state", "connectivity"):
                if field in step:
                    lifecycle[field] = step[field]
            return
        if operation == "system.reboot":
            self._advance(state, step["downtime_ms"], uptime=False)
            clock = state["clock"]
            if clock["boot_generation"] == (1 << 32) - 1:
                raise DoodadError("boot generation overflow")
            clock["uptime_ms"] = 0
            clock["boot_generation"] += 1
            state["lifecycle"] = {
                "app_state": "stopped",
                "display_state": "asleep",
                "connectivity": "offline",
            }
            return
        if operation == "provider.emit":
            existing = state["providers"].get(step["provider"])
            if existing is not None and step["revision"] <= existing["revision"]:
                raise DoodadError(
                    f"scenario.steps[{index}] provider revision must increase"
                )
            state["providers"][step["provider"]] = {
                "event": step["event"],
                "revision": step["revision"],
                "status": step["status"],
                "payload": deepcopy(step["payload"]),
                "observed_at_ms": state["clock"]["scenario_ms"],
            }
            return
        if operation == "surface.publish":
            snapshot = step["snapshot"]
            if snapshot["app_id"] != app_id:
                raise DoodadError(
                    f"scenario.steps[{index}] publishes surface state for "
                    f"{snapshot['app_id']!r}, expected {app_id!r}"
                )
            existing = state["surfaces"].get(app_id)
            if (
                existing is not None
                and snapshot["domain_revision"] < existing["domain_revision"]
            ):
                raise DoodadError(
                    f"scenario.steps[{index}] surface revision regressed"
                )
            if snapshot["observed_at_ms"] != state["clock"]["scenario_ms"]:
                raise DoodadError(
                    f"scenario.steps[{index}] surface observed_at_ms must equal "
                    "the deterministic scenario clock"
                )
            state["surfaces"][app_id] = deepcopy(snapshot)
            return
        if operation == "action.dispatch":
            actions = state["actions"]
            actions["count"] += 1
            actions["last_target"] = step["target"]
            actions["last_value"] = deepcopy(step.get("value"))
            return
        if operation == "assert.state":
            for path, expected in step["equals"].items():
                actual = self._resolve(state, path, index)
                if actual != expected:
                    raise DoodadError(
                        f"scenario.steps[{index}] assertion failed at {path}: "
                        f"expected {expected!r}, got {actual!r}"
                    )
            return
        raise DoodadError(f"scenario.steps[{index}] has unsupported op {operation!r}")

    @staticmethod
    def _resolve(state: dict[str, Any], path: str, index: int) -> Any:
        value: Any = state
        segments = path.split(".")
        cursor = 0
        while cursor < len(segments):
            if not isinstance(value, dict):
                raise DoodadError(
                    f"scenario.steps[{index}] assertion path does not exist: {path}"
                )
            matched: str | None = None
            matched_end = cursor
            for end in range(len(segments), cursor, -1):
                candidate = ".".join(segments[cursor:end])
                if candidate in value:
                    matched = candidate
                    matched_end = end
                    break
            if matched is None:
                raise DoodadError(
                    f"scenario.steps[{index}] assertion path does not exist: {path}"
                )
            value = value[matched]
            cursor = matched_end
        return value
