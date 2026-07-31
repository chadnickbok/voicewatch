from __future__ import annotations

import math
import struct
from typing import Any

from .appspec import validate_appspec
from .contract import DoodadError


COMPONENT = {
    "screen": 0,
    "column": 1,
    "row": 2,
    "scroll": 3,
    "text": 4,
    "button": 5,
    "card": 6,
    "progress": 7,
    "stepper": 8,
    "toggle": 9,
    "keypad": 10,
    "voice_orb": 11,
    "live_card": 12,
    "image": 13,
}
EVENT = {
    "tap": 0,
    "longPress": 1,
    "repeat": 2,
    "valueChanging": 3,
    "valueCommitted": 4,
    "checkedChanged": 5,
    "pageChanged": 6,
    "dismissed": 7,
    "submit": 8,
    "retry": 9,
    "cancel": 10,
}
TONE = {
    "primary": 0,
    "secondary": 1,
    "tertiary": 2,
    "neutral": 3,
    "error": 4,
}
SIZE = {"compact": 0, "default": 1, "large": 2}
GAP = {"none": 0, "xs": 1, "sm": 2, "md": 3, "lg": 4}
ALIGN = {"start": 0, "center": 1, "end": 2, "stretch": 3}
TEXT_STYLE = {
    "display": 0,
    "title": 1,
    "label": 2,
    "body": 3,
    "numeral": 4,
    "caption": 5,
}
BUTTON_VARIANT = {"filled": 0, "tonal": 1, "outlined": 2, "text": 3}
PROGRESS_STYLE = {"linear": 0, "circular": 1, "segmented": 2}
IMAGE_FIT = {"cover": 0, "contain": 1}


def _literal(value: Any, field: str) -> Any:
    if isinstance(value, dict):
        raise DoodadError(
            f"{field} binding lowering requires the binding wire extension"
        )
    return value


def _properties(node: dict[str, Any]) -> dict[int, Any]:
    kind = node["type"]
    props = node["props"]
    if kind in {"screen", "column", "row", "scroll"}:
        return {
            8: GAP[props.get("gap", "md")],
            9: ALIGN[props.get("align", "center")],
        }
    if kind == "text":
        result = {
            0: _literal(props["text"], f"{node['id']}.text"),
            4: TEXT_STYLE[props.get("style", "body")],
            9: ALIGN[props.get("align", "center")],
        }
        if "max_lines" in props:
            result[15] = props["max_lines"]
        return result
    if kind == "button":
        result = {
            0: props["label"],
            4: BUTTON_VARIANT[props.get("variant", "filled")],
            5: TONE[props.get("tone", "primary")],
            6: SIZE[props.get("size", "default")],
        }
        if "icon" in props:
            result[14] = props["icon"]
        return result
    if kind == "card":
        return {
            0: _literal(props["title"], f"{node['id']}.title"),
            1: _literal(props["body"], f"{node['id']}.body"),
            5: TONE[props.get("tone", "neutral")],
        }
    if kind == "progress":
        return {
            0: props.get("label", ""),
            2: _literal(props["value"], f"{node['id']}.value"),
            3: props["maximum"],
            4: PROGRESS_STYLE[props.get("style", "linear")],
            5: TONE[props.get("tone", "primary")],
        }
    if kind == "stepper":
        return {
            0: props["label"],
            1: props.get("unit", ""),
            2: _literal(props["value"], f"{node['id']}.value"),
            3: props["maximum"],
            12: props["minimum"],
            13: props["step"],
        }
    if kind == "toggle":
        return {
            0: props["label"],
            5: TONE[props.get("tone", "primary")],
            7: _literal(props["checked"], f"{node['id']}.checked"),
        }
    if kind == "keypad":
        return {10: props["keys"], 11: props.get("columns", 4)}
    if kind == "voice_orb":
        state = props["state"]
        label = {
            "idle": "Talk",
            "listening": "Listening",
            "thinking": "Thinking",
            "speaking": "Speaking",
            "error": "Try again",
        }[state]
        return {
            0: label,
            1: props.get("transcript", ""),
            5: TONE["primary"],
            16: {
                "idle": 0,
                "listening": 1,
                "thinking": 2,
                "speaking": 3,
                "error": 4,
            }[state],
        }
    if kind == "live_card":
        result: dict[int, Any] = {
            0: _literal(props["title"], f"{node['id']}.title"),
            1: _literal(props["body"], f"{node['id']}.body"),
            5: TONE[props.get("tone", "neutral")],
        }
        if "progress" in props:
            progress = _literal(
                props["progress"], f"{node['id']}.progress"
            )
            result[2] = int(math.floor(progress * 100 + 0.5))
            result[3] = 100
        return result
    if kind == "image":
        return {
            0: props["asset"],
            4: IMAGE_FIT[props.get("fit", "cover")],
        }
    raise DoodadError(f"cannot lower AppSpec component {kind!r}")


def _flatten(
    node: dict[str, Any],
    parent: int | None,
    output: list[dict[int, Any]],
) -> None:
    index = len(output)
    wire: dict[int, Any] = {
        0: node["id"],
        1: COMPONENT[node["type"]],
        2: parent,
        3: _properties(node),
    }
    visible = node.get("visible", True)
    enabled = node.get("enabled", True)
    if isinstance(visible, dict) or isinstance(enabled, dict):
        raise DoodadError(
            f"{node['id']} binding lowering requires the binding wire extension"
        )
    if not visible:
        wire[4] = False
    if not enabled:
        wire[5] = False
    semantics = node.get("semantics", {})
    if semantics.get("label"):
        wire[6] = semantics["label"]
    events = node.get("events", {})
    if events:
        wire[7] = [
            {0: EVENT[name], 1: action}
            for name, action in sorted(
                events.items(), key=lambda item: EVENT[item[0]]
            )
        ]
    if semantics.get("value"):
        wire[8] = semantics["value"]
    if semantics.get("hint"):
        wire[9] = semantics["hint"]
    output.append(wire)
    for child in node.get("props", {}).get("children", []):
        _flatten(child, index, output)


def compile_canonical_cbor(document: dict[str, Any]) -> bytes:
    validate_appspec(document)
    nodes: list[dict[int, Any]] = []
    _flatten(document["screen"], None, nodes)
    encoded = _encode({0: 1, 1: document["app_id"], 2: nodes})
    if len(encoded) > 4096:
        raise DoodadError("canonical AppSpec exceeds the 4096-byte device limit")
    return encoded


def _head(major: int, argument: int) -> bytes:
    if argument < 24:
        return bytes([(major << 5) | argument])
    if argument <= 0xFF:
        return bytes([(major << 5) | 24, argument])
    if argument <= 0xFFFF:
        return bytes([(major << 5) | 25]) + struct.pack(">H", argument)
    if argument <= 0xFFFFFFFF:
        return bytes([(major << 5) | 26]) + struct.pack(">I", argument)
    return bytes([(major << 5) | 27]) + struct.pack(">Q", argument)


def _encode(value: Any) -> bytes:
    if value is None:
        return b"\xf6"
    if value is False:
        return b"\xf4"
    if value is True:
        return b"\xf5"
    if isinstance(value, int):
        return (
            _head(0, value)
            if value >= 0
            else _head(1, -1 - value)
        )
    if isinstance(value, str):
        data = value.encode("utf-8")
        return _head(3, len(data)) + data
    if isinstance(value, list):
        return _head(4, len(value)) + b"".join(_encode(item) for item in value)
    if isinstance(value, dict):
        entries = [(_encode(key), _encode(item)) for key, item in value.items()]
        entries.sort(key=lambda entry: (len(entry[0]), entry[0]))
        return _head(5, len(entries)) + b"".join(
            key + item for key, item in entries
        )
    raise TypeError(f"cannot encode {type(value).__name__} as canonical CBOR")
