from __future__ import annotations

from typing import Any

from .contract import DoodadError


STYLE_MAP = {
    "display": 0,
    "title": 1,
    "body": 2,
    "caption": 3,
    "muted": 4,
}
DIRECTION_MAP = {"column": 0, "row": 1}
ALIGN_MAP = {"start": 0, "center": 1, "end": 2, "stretch": 3}


def _string(value: Any, name: str, maximum: int, minimum: int = 0) -> str:
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        raise DoodadError(f"{name} must contain {minimum}..{maximum} characters")
    return value


def validate_ui(document: dict[str, Any]) -> None:
    if set(document) != {"schema_version", "root"}:
        raise DoodadError("ui.json must contain exactly schema_version and root")
    if document["schema_version"] != 0:
        raise DoodadError("ui.json schema_version must be 0")
    count = [0]
    _validate_node(document["root"], depth=0, count=count, root=True)


def _validate_node(node: Any, depth: int, count: list[int], root: bool = False) -> None:
    if depth > 8:
        raise DoodadError("ui.json nesting exceeds 8 levels")
    count[0] += 1
    if count[0] > 64:
        raise DoodadError("ui.json contains more than 64 nodes")
    if not isinstance(node, dict):
        raise DoodadError("each UI node must be an object")

    node_type = node.get("type")
    if root and node_type != "stack":
        raise DoodadError("ui.json root must be a stack")
    if node_type == "stack":
        allowed = {"type", "direction", "align", "gap", "children"}
        if set(node) - allowed or "children" not in node:
            raise DoodadError("stack has missing or unknown fields")
        if node.get("direction", "column") not in DIRECTION_MAP:
            raise DoodadError("stack direction must be column or row")
        if node.get("align", "center") not in ALIGN_MAP:
            raise DoodadError("stack align must be start, center, end, or stretch")
        gap = node.get("gap", 8)
        if not isinstance(gap, int) or not 0 <= gap <= 32:
            raise DoodadError("stack gap must be an integer from 0 through 32")
        children = node["children"]
        if not isinstance(children, list) or len(children) > 32:
            raise DoodadError("stack children must be an array of at most 32 nodes")
        for child in children:
            _validate_node(child, depth + 1, count)
    elif node_type == "text":
        if set(node) - {"type", "text", "style"} or "text" not in node:
            raise DoodadError("text has missing or unknown fields")
        _string(node["text"], "text", 256)
        if node.get("style", "body") not in STYLE_MAP:
            raise DoodadError(f"unknown text style {node.get('style')!r}")
    elif node_type == "button":
        if set(node) - {"type", "id", "label", "disabled"}:
            raise DoodadError("button has unknown fields")
        identifier = _string(node.get("id"), "button id", 64, 1)
        if not identifier[0].islower() or any(
            not (character.islower() or character.isdigit() or character in "_.-")
            for character in identifier
        ):
            raise DoodadError(f"invalid button id {identifier!r}")
        _string(node.get("label"), "button label", 64, 1)
        if not isinstance(node.get("disabled", False), bool):
            raise DoodadError("button disabled must be boolean")
    elif node_type == "progress":
        if set(node) - {"type", "value", "maximum", "label"}:
            raise DoodadError("progress has unknown fields")
        value = node.get("value")
        maximum = node.get("maximum")
        if (
            not isinstance(value, int)
            or not isinstance(maximum, int)
            or maximum < 1
            or not 0 <= value <= maximum
        ):
            raise DoodadError("progress requires 0 <= value <= maximum")
        if "label" in node:
            _string(node["label"], "progress label", 64)
    else:
        raise DoodadError(f"unsupported UI node type {node_type!r}")


def render_ui(host: Any, document: dict[str, Any]) -> None:
    validate_ui(document)
    root = document["root"]
    parent = host.ui_begin_document(
        DIRECTION_MAP[root.get("direction", "column")],
        ALIGN_MAP[root.get("align", "center")],
        root.get("gap", 8),
    )
    for child in root["children"]:
        _render_node(host, parent, child)
    host.render_now()


def _render_node(host: Any, parent: int, node: dict[str, Any]) -> None:
    node_type = node["type"]
    if node_type == "stack":
        stack = host.ui_add_stack(
            parent,
            DIRECTION_MAP[node.get("direction", "column")],
            ALIGN_MAP[node.get("align", "center")],
            node.get("gap", 8),
        )
        for child in node["children"]:
            _render_node(host, stack, child)
    elif node_type == "text":
        host.ui_add_text(parent, node["text"], STYLE_MAP[node.get("style", "body")])
    elif node_type == "button":
        host.ui_add_button(
            parent,
            node["id"],
            node["label"],
            node.get("disabled", False),
        )
    elif node_type == "progress":
        host.ui_add_progress(
            parent,
            node.get("label", ""),
            node["value"],
            node["maximum"],
        )
