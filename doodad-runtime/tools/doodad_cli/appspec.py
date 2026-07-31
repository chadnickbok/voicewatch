from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any

from .contract import DoodadError


LAYOUT_TYPES = {"screen", "column", "row", "scroll"}
LEAF_TYPES = {
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
NODE_TYPES = LAYOUT_TYPES | LEAF_TYPES
INTERACTIVE_TYPES = {"button", "stepper", "toggle", "keypad", "voice_orb"}
TONES = {"primary", "secondary", "tertiary", "neutral", "error"}
SIZES = {"compact", "default", "large"}
SPACING = {"none", "xs", "sm", "md", "lg"}
TEXT_STYLES = {
    "display",
    "title",
    "label",
    "body",
    "numeral",
    "caption",
}
EVENTS = {
    "tap",
    "longPress",
    "repeat",
    "valueChanging",
    "valueCommitted",
    "checkedChanged",
    "pageChanged",
    "dismissed",
    "submit",
    "retry",
    "cancel",
}
STATE_PATH = re.compile(
    r"^(screen|app|shared|system|session)\.[a-zA-Z0-9_-]+"
    r"(?:\.[a-zA-Z0-9_-]+)*$"
)
PREDICATES = {
    "exists",
    "equals",
    "not_equals",
    "less_than",
    "greater_than",
}
FORMATS = {"raw", "number", "duration"}


@dataclass(frozen=True)
class AppSpecStats:
    nodes: int
    maximum_depth: int
    primary_scroll_axes: int
    component_types: frozenset[str]


def _identifier(value: Any, field: str) -> str:
    if (
        not isinstance(value, str)
        or not 1 <= len(value) <= 64
        or not value[0].islower()
        or any(
            not (character.islower() or character.isdigit() or character in "_.-")
            for character in value
        )
    ):
        raise DoodadError(f"{field} must be a lowercase semantic identifier")
    return value


def _text(value: Any, field: str, maximum: int = 256) -> str:
    if not isinstance(value, str) or not 1 <= len(value) <= maximum:
        raise DoodadError(f"{field} must contain 1..{maximum} characters")
    return value


def _binding(value: Any, field: str, *, allow_format: bool) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DoodadError(f"{field} must be a literal or binding")
    allowed = {"bind", "predicate"} | ({"format"} if allow_format else set())
    if set(value) - allowed or "bind" not in value:
        raise DoodadError(f"{field} has an invalid binding shape")
    if not isinstance(value["bind"], str) or not STATE_PATH.fullmatch(value["bind"]):
        raise DoodadError(f"{field}.bind must be a valid typed state path")

    predicate = value.get("predicate")
    if predicate is not None:
        if (
            not isinstance(predicate, dict)
            or set(predicate) - {"op", "value"}
            or predicate.get("op") not in PREDICATES
        ):
            raise DoodadError(f"{field}.predicate is unsupported")
        if predicate["op"] == "exists":
            if set(predicate) != {"op"}:
                raise DoodadError(f"{field}.predicate exists takes no value")
        elif set(predicate) != {"op", "value"} or not isinstance(
            predicate["value"], (bool, int, str)
        ):
            raise DoodadError(f"{field}.predicate requires a scalar value")

    formatting = value.get("format")
    if formatting is not None:
        if (
            not isinstance(formatting, dict)
            or set(formatting) - {"kind", "unit"}
            or formatting.get("kind") not in FORMATS
        ):
            raise DoodadError(f"{field}.format is unsupported")
        if formatting["kind"] == "number":
            unit = formatting.get("unit", "")
            if not isinstance(unit, str) or len(unit) > 16:
                raise DoodadError(f"{field}.format.unit is too long")
        elif set(formatting) != {"kind"}:
            raise DoodadError(
                f"{field}.format.unit is only valid for number formatting"
            )
    return value


def _text_or_binding(value: Any, field: str, maximum: int = 256) -> None:
    if isinstance(value, dict):
        _binding(value, field, allow_format=True)
    else:
        _text(value, field, maximum)


def _typed_or_binding(
    value: Any,
    field: str,
    literal_type: type,
    *,
    allow_format: bool = False,
) -> None:
    if isinstance(value, dict):
        _binding(value, field, allow_format=allow_format)
    elif literal_type is int:
        if not isinstance(value, int) or isinstance(value, bool):
            raise DoodadError(f"{field} must be an integer or binding")
    elif not isinstance(value, literal_type):
        raise DoodadError(
            f"{field} must be {literal_type.__name__} or a binding"
        )


def validate_appspec(document: dict[str, Any]) -> AppSpecStats:
    if not isinstance(document, dict) or set(document) != {
        "schema_version",
        "app_id",
        "screen",
    }:
        raise DoodadError(
            "AppSpec must contain exactly schema_version, app_id, and screen"
        )
    if document["schema_version"] != 1:
        raise DoodadError("AppSpec schema_version must be 1")
    _identifier(document["app_id"], "app_id")
    seen: set[str] = set()
    state = {
        "count": 0,
        "max_depth": 0,
        "scrolls": 0,
        "types": set(),
    }
    _validate_node(document["screen"], 0, seen, state, root=True)
    if state["scrolls"] > 1:
        raise DoodadError("a screen may contain only one primary scroll axis")
    return AppSpecStats(
        nodes=state["count"],
        maximum_depth=state["max_depth"],
        primary_scroll_axes=state["scrolls"],
        component_types=frozenset(state["types"]),
    )


def _validate_node(
    node: Any,
    depth: int,
    seen: set[str],
    state: dict[str, Any],
    *,
    root: bool = False,
) -> None:
    if not isinstance(node, dict):
        raise DoodadError("every AppSpec node must be an object")
    if depth > 12:
        raise DoodadError("AppSpec tree depth exceeds 12")
    state["count"] += 1
    state["max_depth"] = max(state["max_depth"], depth)
    if state["count"] > 250:
        raise DoodadError("AppSpec contains more than 250 nodes")

    node_type = node.get("type")
    if node_type not in NODE_TYPES:
        raise DoodadError(f"unsupported AppSpec component {node_type!r}")
    if root and node_type != "screen":
        raise DoodadError("AppSpec root must be a screen")
    identifier = _identifier(node.get("id"), "node id")
    if identifier in seen:
        raise DoodadError(f"duplicate AppSpec node id {identifier!r}")
    seen.add(identifier)
    state["types"].add(node_type)

    common = {"id", "type", "visible", "enabled", "props", "events", "semantics"}
    if set(node) - common:
        raise DoodadError(f"{identifier} has unsupported fields")
    for field in ("visible", "enabled"):
        if field not in node:
            continue
        if isinstance(node[field], dict):
            _binding(
                node[field], f"{identifier}.{field}", allow_format=False
            )
        elif not isinstance(node[field], bool):
            raise DoodadError(
                f"{identifier}.{field} must be bool or a binding"
            )
    props = node.get("props", {})
    events = node.get("events", {})
    semantics = node.get("semantics", {})
    if not isinstance(props, dict) or not isinstance(events, dict):
        raise DoodadError(f"{identifier} props and events must be objects")
    if not isinstance(semantics, dict):
        raise DoodadError(f"{identifier}.semantics must be an object")
    if set(semantics) - {"label", "value", "hint"}:
        raise DoodadError(f"{identifier}.semantics has unsupported fields")
    for field in ("label", "value", "hint"):
        if field in semantics:
            _text(
                semantics[field],
                f"{identifier} semantics.{field}",
                128,
            )
    if set(events) - EVENTS:
        raise DoodadError(f"{identifier} uses an unsupported semantic event")
    for action in events.values():
        _identifier(action, f"{identifier} action")
    if node_type in INTERACTIVE_TYPES:
        _text(semantics.get("label"), f"{identifier} semantics.label", 128)
        if not events:
            raise DoodadError(f"{identifier} is interactive but has no event")
    if node_type == "image":
        _text(semantics.get("label"), f"{identifier} semantics.label", 128)

    if node_type in LAYOUT_TYPES:
        allowed = {"children", "gap", "align"}
        if set(props) - allowed:
            raise DoodadError(f"{identifier} layout has unsupported props")
        children = props.get("children")
        if not isinstance(children, list) or len(children) > 32:
            raise DoodadError(f"{identifier}.children must contain at most 32 nodes")
        if props.get("gap", "md") not in SPACING:
            raise DoodadError(f"{identifier}.gap must use a spacing token")
        if props.get("align", "center") not in {"start", "center", "end", "stretch"}:
            raise DoodadError(f"{identifier}.align is unsupported")
        if node_type == "scroll":
            state["scrolls"] += 1
        for child in children:
            _validate_node(child, depth + 1, seen, state)
        return

    validators = {
        "text": _validate_text,
        "button": _validate_button,
        "card": _validate_card,
        "progress": _validate_progress,
        "stepper": _validate_stepper,
        "toggle": _validate_toggle,
        "keypad": _validate_keypad,
        "voice_orb": _validate_voice_orb,
        "live_card": _validate_live_card,
        "image": _validate_image,
    }
    validators[node_type](identifier, props)


def _exact_props(identifier: str, props: dict[str, Any], allowed: set[str]) -> None:
    if set(props) - allowed:
        forbidden = ", ".join(sorted(set(props) - allowed))
        raise DoodadError(f"{identifier} has unsupported semantic props: {forbidden}")


def _tone_size(identifier: str, props: dict[str, Any]) -> None:
    if props.get("tone", "primary") not in TONES:
        raise DoodadError(f"{identifier}.tone is unsupported")
    if props.get("size", "default") not in SIZES:
        raise DoodadError(f"{identifier}.size is unsupported")


def _validate_text(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"text", "style", "max_lines", "align"})
    _text_or_binding(props.get("text"), f"{identifier}.text")
    if props.get("style", "body") not in TEXT_STYLES:
        raise DoodadError(f"{identifier}.style is unsupported")
    if not isinstance(props.get("max_lines", 2), int) or not 1 <= props.get(
        "max_lines", 2
    ) <= 4:
        raise DoodadError(f"{identifier}.max_lines must be 1..4")


def _validate_button(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"label", "tone", "variant", "size", "icon"})
    _text(props.get("label"), f"{identifier}.label", 64)
    _tone_size(identifier, props)
    if props.get("variant", "filled") not in {"filled", "tonal", "outlined", "text"}:
        raise DoodadError(f"{identifier}.variant is unsupported")


def _validate_card(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"title", "body", "tone"})
    _text_or_binding(props.get("title"), f"{identifier}.title", 64)
    _text_or_binding(props.get("body"), f"{identifier}.body")
    _tone_size(identifier, props)


def _validate_progress(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"label", "value", "maximum", "style", "tone"})
    value, maximum = props.get("value"), props.get("maximum")
    _typed_or_binding(value, f"{identifier}.value", int)
    if not isinstance(maximum, int) or isinstance(maximum, bool) or maximum < 1:
        raise DoodadError(f"{identifier}.maximum must be a positive integer")
    if isinstance(value, int) and not isinstance(value, bool) and not 0 <= value <= maximum:
        raise DoodadError(f"{identifier} requires 0 <= value <= maximum")
    if props.get("style", "linear") not in {"linear", "circular", "segmented"}:
        raise DoodadError(f"{identifier}.style is unsupported")
    _tone_size(identifier, props)


def _validate_stepper(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(
        identifier, props, {"label", "value", "unit", "minimum", "maximum", "step"}
    )
    _text(props.get("label"), f"{identifier}.label", 64)
    value = props.get("value")
    _typed_or_binding(
        value, f"{identifier}.value", int, allow_format=True
    )
    minimum, maximum, step = [
        props.get(key) for key in ("minimum", "maximum", "step")
    ]
    if not all(
        isinstance(item, int) and not isinstance(item, bool)
        for item in (minimum, maximum, step)
    ):
        raise DoodadError(f"{identifier} stepper bounds must be integers")
    if (
        step <= 0
        or minimum > maximum
        or (
            isinstance(value, int)
            and not isinstance(value, bool)
            and not minimum <= value <= maximum
        )
    ):
        raise DoodadError(f"{identifier} has an invalid stepper range")


def _validate_toggle(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"label", "checked", "tone"})
    _text(props.get("label"), f"{identifier}.label", 64)
    _typed_or_binding(
        props.get("checked"), f"{identifier}.checked", bool
    )
    _tone_size(identifier, props)


def _validate_keypad(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"keys", "columns"})
    keys = props.get("keys")
    columns = props.get("columns", 4)
    if (
        not isinstance(keys, list)
        or not 1 <= len(keys) <= 20
        or not all(isinstance(key, str) and 1 <= len(key) <= 4 for key in keys)
    ):
        raise DoodadError(f"{identifier}.keys must contain 1..20 short labels")
    if not isinstance(columns, int) or not 2 <= columns <= 5:
        raise DoodadError(f"{identifier}.columns must be 2..5")


def _validate_voice_orb(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"state", "transcript"})
    if props.get("state") not in {
        "idle",
        "listening",
        "thinking",
        "speaking",
        "error",
    }:
        raise DoodadError(f"{identifier}.state is unsupported")


def _validate_live_card(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"title", "body", "progress", "tone"})
    _text_or_binding(props.get("title"), f"{identifier}.title", 64)
    _text_or_binding(props.get("body"), f"{identifier}.body")
    if "progress" in props:
        progress = props["progress"]
        if isinstance(progress, dict):
            _binding(
                progress,
                f"{identifier}.progress",
                allow_format=False,
            )
        elif (
            isinstance(progress, bool)
            or not isinstance(progress, (int, float))
            or not 0 <= progress <= 1
        ):
            raise DoodadError(
                f"{identifier}.progress must be 0..1 or a binding"
            )
    _tone_size(identifier, props)


def _validate_image(identifier: str, props: dict[str, Any]) -> None:
    _exact_props(identifier, props, {"asset", "fit"})
    asset = props.get("asset")
    if (
        not isinstance(asset, str)
        or re.fullmatch(r"[0-9a-f]{64}", asset) is None
    ):
        raise DoodadError(
            f"{identifier}.asset must be a lowercase SHA-256 digest"
        )
    if props.get("fit", "cover") not in {"cover", "contain"}:
        raise DoodadError(f"{identifier}.fit is unsupported")


def compile_to_ui_v0(document: dict[str, Any]) -> dict[str, Any]:
    """Compile the semantic contract to the current minimal host ABI.

    This compatibility compiler intentionally loses decorative detail. Native
    firmware/catalog rendering uses m3e_lvgl directly; generated apps remain
    unable to name LVGL widgets, colors, radii, or coordinates.
    """
    validate_appspec(document)
    root = document["screen"]
    return {
        "schema_version": 0,
        "root": {
            "type": "stack",
            "direction": "column",
            "align": root.get("props", {}).get("align", "center"),
            "gap": _gap_px(root.get("props", {}).get("gap", "md")),
            "children": [
                _compile_node(child) for child in root["props"]["children"]
            ],
        },
    }


def _gap_px(value: str) -> int:
    return {"none": 0, "xs": 4, "sm": 8, "md": 12, "lg": 16}[value]


def _preview_text(value: Any) -> str:
    if isinstance(value, dict):
        return f"{{{value['bind']}}}"
    return str(value)


def _preview_integer(value: Any) -> int:
    return 0 if isinstance(value, dict) else value


def _preview_boolean(value: Any) -> bool:
    return False if isinstance(value, dict) else value


def _compile_node(node: dict[str, Any]) -> dict[str, Any]:
    kind, props = node["type"], node.get("props", {})
    if kind in LAYOUT_TYPES:
        return {
            "type": "stack",
            "direction": "row" if kind == "row" else "column",
            "align": props.get("align", "center"),
            "gap": _gap_px(props.get("gap", "md")),
            "children": [_compile_node(child) for child in props["children"]],
        }
    if kind == "text":
        style = props.get("style", "body")
        return {
            "type": "text",
            "text": _preview_text(props["text"]),
            "style": {
                "display": "display",
                "title": "title",
                "label": "caption",
                "body": "body",
                "numeral": "display",
                "caption": "muted",
            }[style],
        }
    if kind == "button":
        return {"type": "button", "id": node["id"], "label": props["label"]}
    if kind == "progress":
        return {
            "type": "progress",
            "label": props.get("label", ""),
            "value": _preview_integer(props["value"]),
            "maximum": props["maximum"],
        }
    if kind in {"card", "live_card"}:
        return {
            "type": "stack",
            "direction": "column",
            "align": "start",
            "gap": 4,
            "children": [
                {
                    "type": "text",
                    "text": _preview_text(props["title"]),
                    "style": "title",
                },
                {
                    "type": "text",
                    "text": _preview_text(props["body"]),
                    "style": "muted",
                },
            ],
        }
    if kind == "stepper":
        return {
            "type": "stack",
            "direction": "row",
            "align": "center",
            "gap": 8,
            "children": [
                {"type": "button", "id": f"{node['id']}.down", "label": "-"},
                {
                    "type": "text",
                    "text": (
                        f"{_preview_text(props['value'])} "
                        f"{props.get('unit', '')}"
                    ).strip(),
                    "style": "title",
                },
                {"type": "button", "id": f"{node['id']}.up", "label": "+"},
            ],
        }
    if kind == "toggle":
        return {
            "type": "button",
            "id": node["id"],
            "label": (
                f"{props['label']}: "
                f"{'On' if _preview_boolean(props['checked']) else 'Off'}"
            ),
        }
    if kind == "keypad":
        rows = [
            props["keys"][index : index + props.get("columns", 4)]
            for index in range(0, len(props["keys"]), props.get("columns", 4))
        ]
        return {
            "type": "stack",
            "direction": "column",
            "align": "stretch",
            "gap": 4,
            "children": [
                {
                    "type": "stack",
                    "direction": "row",
                    "align": "stretch",
                    "gap": 4,
                    "children": [
                        {
                            "type": "button",
                            "id": f"{node['id']}.{row_index}.{column_index}",
                            "label": label,
                        }
                        for column_index, label in enumerate(row)
                    ],
                }
                for row_index, row in enumerate(rows)
            ],
        }
    if kind == "voice_orb":
        return {
            "type": "button",
            "id": node["id"],
            "label": {
                "idle": "Talk",
                "listening": "Listening...",
                "thinking": "Thinking...",
                "speaking": "Speaking...",
                "error": "Try again",
            }[props["state"]],
        }
    if kind == "image":
        return {
            "type": "text",
            "text": node.get("semantics", {}).get("label", "Image"),
            "style": "muted",
        }
    raise DoodadError(f"cannot compile AppSpec component {kind!r}")
