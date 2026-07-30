#!/usr/bin/env python3
"""Synchronize core Wear Material 3 tokens from the frozen AndroidX source.

The normalized JSON is the canonical input to generated runtime types. Network
access is needed only for --refresh. Normal builds and --check are offline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import urllib.request
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
CONFIG_PATH = pathlib.Path(__file__).with_name("pinned_sources.json")
NORMALIZED_PATH = ROOT / "reference/material-tokens/material_wear_1_6_2.json"
HEADER_PATH = ROOT / "components/m3e_lvgl/include/m3e/generated/core_tokens.hpp"


def read_json(path: pathlib.Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def git_blob_id(content: bytes) -> str:
    prefix = f"blob {len(content)}\0".encode()
    return hashlib.sha1(prefix + content).hexdigest()


def fetch_bytes(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read()


def gitiles_json(url: str) -> Any:
    raw = fetch_bytes(url)
    if raw.startswith(b")]}'\n"):
        raw = raw[5:]
    return json.loads(raw)


def camel_to_snake(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def source_line(text: str, needle: str) -> int:
    return text[: text.index(needle)].count("\n") + 1


def parse_enum(text: str, enum_name: str) -> list[str]:
    match = re.search(
        rf"(?:internal )?enum class {re.escape(enum_name)}\s*\{{(?P<body>.*?)\}}",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing enum {enum_name}")
    return re.findall(r"^\s*([A-Z][A-Za-z0-9]+),\s*$", match.group("body"), re.MULTILINE)


def parse_palette(text: str) -> dict[str, dict[str, Any]]:
    pattern = re.compile(
        r"val (?P<name>\w+) = Color\(red = (?P<r>\d+), "
        r"green = (?P<g>\d+), blue = (?P<b>\d+)\)"
    )
    result: dict[str, dict[str, Any]] = {}
    for match in pattern.finditer(text):
        result[match["name"]] = {
            "rgb888": [
                int(match["r"]),
                int(match["g"]),
                int(match["b"]),
            ],
            "origin": {
                "file": "PaletteTokens.kt",
                "line": text[: match.start()].count("\n") + 1,
            },
        }
    if not result:
        raise ValueError("no palette colors parsed")
    return result


def parse_color_roles(
    text: str, role_names: list[str], palette: dict[str, dict[str, Any]]
) -> list[dict[str, Any]]:
    mappings = {
        match["role"]: (match["palette"], text[: match.start()].count("\n") + 1)
        for match in re.finditer(
            r"val (?P<role>\w+) = PaletteTokens\.(?P<palette>\w+)", text
        )
    }
    roles = []
    for name in role_names:
        palette_name, line = mappings[name]
        roles.append(
            {
                "name": camel_to_snake(name),
                "upstream_name": name,
                "palette_token": palette_name,
                "rgb888": palette[palette_name]["rgb888"],
                "origin": {"file": "ColorTokens.kt", "line": line},
            }
        )
    return roles


def numeric_value(expression: str) -> float:
    match = re.search(r"-?\d+(?:\.\d+)?", expression)
    if not match:
        raise ValueError(f"no numeric value in {expression!r}")
    return float(match.group(0))


def parse_typography(
    type_scale: str, flat_roles: list[str], arc_roles: list[str]
) -> list[dict[str, Any]]:
    assignments: dict[str, tuple[str, int]] = {}
    for match in re.finditer(r"^\s*val (?P<key>\w+) = (?P<value>[^\n]+)$", type_scale, re.MULTILINE):
        assignments[match["key"]] = (
            match["value"].strip(),
            type_scale[: match.start()].count("\n") + 1,
        )

    result = []
    for name in flat_roles + arc_roles:
        is_arc = name.startswith("Arc")
        tracking_key = f"{name}TrackingTop" if is_arc else f"{name}Tracking"
        bottom_key = f"{name}TrackingBottom" if is_arc else tracking_key
        prominent_key = f"{name}WeightProminent"
        role = {
            "name": camel_to_snake(name),
            "upstream_name": name,
            "arc": is_arc,
            "font": assignments[f"{name}Font"][0].split(".")[-1],
            "size_sp": numeric_value(assignments[f"{name}Size"][0]),
            "line_height_sp": numeric_value(assignments[f"{name}LineHeight"][0]),
            "tracking_top_sp": numeric_value(assignments[tracking_key][0]),
            "tracking_bottom_sp": numeric_value(assignments[bottom_key][0]),
            "weight": numeric_value(assignments[f"{name}Weight"][0]),
            "prominent_weight": (
                numeric_value(assignments[prominent_key][0])
                if prominent_key in assignments
                else None
            ),
            "width": numeric_value(assignments[f"{name}Width"][0]),
            "origin": {
                "file": "TypeScaleTokens.kt",
                "line": assignments[f"{name}Size"][1],
            },
        }
        result.append(role)
    return result


def parse_shapes(text: str, names: list[str]) -> list[dict[str, Any]]:
    result = []
    for name in names:
        needle = f"val {name} = "
        line = source_line(text, needle)
        expression = text.split(needle, 1)[1].splitlines()[0].strip()
        if expression == "CircleShape":
            kind, radius = "full", None
        elif expression == "RectangleShape":
            kind, radius = "none", 0.0
        else:
            kind, radius = "rounded", numeric_value(expression)
        result.append(
            {
                "name": camel_to_snake(name.removeprefix("Corner")),
                "upstream_name": name,
                "kind": kind,
                "radius_dp": radius,
                "origin": {"file": "ShapeTokens.kt", "line": line},
            }
        )
    return result


def parse_motion(text: str) -> dict[str, Any]:
    durations = []
    for match in re.finditer(
        r"const val Duration(?P<name>\w+) = (?P<value>\d+)", text
    ):
        durations.append(
            {
                "name": camel_to_snake(match["name"]),
                "upstream_name": f"Duration{match['name']}",
                "milliseconds": int(match["value"]),
                "origin": {
                    "file": "MotionTokens.kt",
                    "line": text[: match.start()].count("\n") + 1,
                },
            }
        )
    easings = []
    for match in re.finditer(
        r"val Easing(?P<name>\w+) = CubicBezierEasing\("
        r"(?P<x1>[\d.]+)f, (?P<y1>[\d.]+)f, "
        r"(?P<x2>[\d.]+)f, (?P<y2>[\d.]+)f\)",
        text,
    ):
        easings.append(
            {
                "name": camel_to_snake(match["name"]),
                "upstream_name": f"Easing{match['name']}",
                "kind": "cubic_bezier",
                "control_points": [
                    float(match["x1"]),
                    float(match["y1"]),
                    float(match["x2"]),
                    float(match["y2"]),
                ],
                "origin": {
                    "file": "MotionTokens.kt",
                    "line": text[: match.start()].count("\n") + 1,
                },
            }
        )
    path_match = re.search(
        r"val EasingEmphasizedStandard\s*=.*?parsePathString\(\s*\"([^\"]+)\"",
        text,
        re.DOTALL,
    )
    if not path_match:
        raise ValueError("missing emphasized standard easing path")
    easings.append(
        {
            "name": "emphasized_standard",
            "upstream_name": "EasingEmphasizedStandard",
            "kind": "path",
            "path": path_match.group(1),
            "origin": {
                "file": "MotionTokens.kt",
                "line": text[: path_match.start()].count("\n") + 1,
            },
        }
    )
    easings.sort(key=lambda value: value["upstream_name"])
    return {"durations": durations, "easings": easings}


def refresh() -> dict[str, Any]:
    config = read_json(CONFIG_PATH)
    commit = config["commit"]
    token_path = config["token_path"]
    base = (
        "https://android.googlesource.com/platform/frameworks/support/+/"
        f"{commit}/{token_path}"
    )
    listing = gitiles_json(f"{base}/?format=JSON")
    upstream_entries = {
        entry["name"]: entry["id"]
        for entry in listing["entries"]
        if entry["type"] == "blob" and entry["name"].endswith(".kt")
    }
    contents: dict[str, str] = {}
    sources = []
    for name in sorted(upstream_entries):
        sources.append(
            {
                "file": name,
                "git_blob": upstream_entries[name],
                "url": f"{base}/{name}",
            }
        )
        if name in config["core_files"]:
            blob_url = f"{base}/{name}?format=TEXT"
            import base64

            content = base64.b64decode(fetch_bytes(blob_url))
            actual_blob = git_blob_id(content)
            if actual_blob != upstream_entries[name]:
                raise ValueError(
                    f"blob verification failed for {name}: "
                    f"{actual_blob} != {upstream_entries[name]}"
                )
            contents[name] = content.decode("utf-8")

    missing = sorted(set(config["core_files"]) - set(contents))
    if missing:
        raise ValueError(f"missing pinned core files: {', '.join(missing)}")

    palette = parse_palette(contents["PaletteTokens.kt"])
    color_names = parse_enum(contents["ColorSchemeKeyTokens.kt"], "ColorSchemeKeyTokens")
    flat_type_names = parse_enum(
        contents["TypographyKeyTokens.kt"], "TypographyKeyTokens"
    )
    arc_type_names = parse_enum(
        contents["TypographyKeyTokens.kt"], "ArcTypographyKeyTokens"
    )
    shape_names = parse_enum(contents["ShapeKeyTokens.kt"], "ShapeKeyTokens")
    normalized = {
        "schema_version": 1,
        "artifact": config["artifact"],
        "androidx_commit": commit,
        "token_path": token_path,
        "sources": sources,
        "color_roles": parse_color_roles(
            contents["ColorTokens.kt"], color_names, palette
        ),
        "typography_roles": parse_typography(
            contents["TypeScaleTokens.kt"], flat_type_names, arc_type_names
        ),
        "shape_roles": parse_shapes(contents["ShapeTokens.kt"], shape_names),
        "motion": parse_motion(contents["MotionTokens.kt"]),
    }
    validate(normalized)
    return normalized


def validate(data: dict[str, Any]) -> None:
    if data["androidx_commit"] != read_json(CONFIG_PATH)["commit"]:
        raise ValueError("normalized data uses the wrong AndroidX commit")
    if len(data["color_roles"]) != 29:
        raise ValueError("Wear 1.6.2 must contain exactly 29 color roles")
    if len(data["typography_roles"]) != 21:
        raise ValueError("Wear 1.6.2 must contain exactly 21 typography roles")
    if len(data["shape_roles"]) != 7:
        raise ValueError("Wear 1.6.2 must contain exactly seven shape roles")
    required_colors = {
        "background",
        "on_background",
        "primary",
        "primary_dim",
        "surface_container_low",
        "surface_container",
        "surface_container_high",
        "error",
        "error_dim",
    }
    actual_colors = {role["name"] for role in data["color_roles"]}
    if not required_colors.issubset(actual_colors):
        raise ValueError("normalized data is missing required Wear color roles")


def q8_8(value: float) -> int:
    return round(value * 256)


def cpp_float(value: float) -> str:
    rendered = f"{value:.6g}"
    if "." not in rendered and "e" not in rendered.lower():
        rendered += ".0"
    return f"{rendered}F"


def generate_header(data: dict[str, Any]) -> str:
    color_roles = data["color_roles"]
    type_roles = data["typography_roles"]
    shapes = data["shape_roles"]
    durations = data["motion"]["durations"]
    easings = data["motion"]["easings"]

    lines = [
        "// Generated by tools/token_sync/sync.py. DO NOT EDIT.",
        f"// Source: {data['artifact']}",
        f"// AndroidX: {data['androidx_commit']}",
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace m3e::generated {",
        "",
        "struct ColorRgb888 {",
        "    std::uint8_t red;",
        "    std::uint8_t green;",
        "    std::uint8_t blue;",
        "};",
        "",
        "constexpr std::uint16_t to_rgb565(ColorRgb888 color) {",
        "    return static_cast<std::uint16_t>(",
        "        ((color.red & 0xF8U) << 8U)",
        "        | ((color.green & 0xFCU) << 3U)",
        "        | (color.blue >> 3U));",
        "}",
        "",
        "enum class ColorRole : std::uint8_t {",
    ]
    lines.extend(f"    {role['name']}," for role in color_roles)
    lines.extend(
        [
            "    count,",
            "};",
            "",
            "inline constexpr std::size_t kColorRoleCount =",
            "    static_cast<std::size_t>(ColorRole::count);",
            "",
            "struct WearColorScheme {",
        ]
    )
    lines.extend(f"    ColorRgb888 {role['name']};" for role in color_roles)
    lines.extend(
        [
            "",
            "    constexpr ColorRgb888 get(ColorRole role) const {",
            "        switch (role) {",
        ]
    )
    lines.extend(
        f"            case ColorRole::{role['name']}: return {role['name']};"
        for role in color_roles
    )
    lines.extend(
        [
            "            case ColorRole::count: break;",
            "        }",
            "        return ColorRgb888{0, 0, 0};",
            "    }",
            "};",
            "",
            "inline constexpr WearColorScheme kBaselineDarkColorScheme{",
        ]
    )
    lines.extend(
        f"    ColorRgb888{{{r}, {g}, {b}}},  // {role['upstream_name']} <- {role['palette_token']}"
        for role in color_roles
        for r, g, b in [role["rgb888"]]
    )
    lines.extend(
        [
            "};",
            "",
            "enum class TypographyRole : std::uint8_t {",
        ]
    )
    lines.extend(f"    {role['name']}," for role in type_roles)
    lines.extend(
        [
            "    count,",
            "};",
            "",
            "inline constexpr std::size_t kTypographyRoleCount =",
            "    static_cast<std::size_t>(TypographyRole::count);",
            "",
            "struct TypographyToken {",
            "    std::uint16_t size_sp_q8_8;",
            "    std::uint16_t line_height_sp_q8_8;",
            "    std::int16_t tracking_top_sp_q8_8;",
            "    std::int16_t tracking_bottom_sp_q8_8;",
            "    std::uint16_t weight;",
            "    std::uint16_t prominent_weight;",
            "    std::uint16_t width;",
            "    bool is_arc;",
            "};",
            "",
            "inline constexpr std::array<TypographyToken, kTypographyRoleCount>",
            "    kTypographyTokens{{",
        ]
    )
    for role in type_roles:
        prominent = (
            round(role["prominent_weight"])
            if role["prominent_weight"] is not None
            else 0
        )
        lines.append(
            "        TypographyToken{"
            f"{q8_8(role['size_sp'])}, {q8_8(role['line_height_sp'])}, "
            f"{q8_8(role['tracking_top_sp'])}, "
            f"{q8_8(role['tracking_bottom_sp'])}, "
            f"{round(role['weight'])}, {prominent}, {round(role['width'])}, "
            f"{str(role['arc']).lower()}"
            f"}},  // {role['upstream_name']}"
        )
    lines.extend(
        [
            "    }};",
            "",
            "enum class ShapeRole : std::uint8_t {",
        ]
    )
    lines.extend(f"    {shape['name']}," for shape in shapes)
    lines.extend(
        [
            "    count,",
            "};",
            "",
            "enum class ShapeKind : std::uint8_t { none, rounded, full };",
            "",
            "struct ShapeToken {",
            "    ShapeKind kind;",
            "    std::uint16_t radius_dp_q8_8;",
            "};",
            "",
            "inline constexpr std::array<ShapeToken,",
            "    static_cast<std::size_t>(ShapeRole::count)> kShapeTokens{{",
        ]
    )
    for shape in shapes:
        kind = shape["kind"]
        radius = q8_8(shape["radius_dp"] or 0.0)
        lines.append(
            f"        ShapeToken{{ShapeKind::{kind}, {radius}}},"
            f"  // {shape['upstream_name']}"
        )
    lines.extend(
        [
            "    }};",
            "",
            "enum class MotionDuration : std::uint8_t {",
        ]
    )
    lines.extend(f"    {duration['name']}," for duration in durations)
    lines.extend(
        [
            "    count,",
            "};",
            "",
            "inline constexpr std::array<std::uint16_t,",
            "    static_cast<std::size_t>(MotionDuration::count)>",
            "    kMotionDurationsMs{{",
        ]
    )
    lines.extend(
        f"        {duration['milliseconds']},  // {duration['upstream_name']}"
        for duration in durations
    )
    lines.extend(
        [
            "    }};",
            "",
            "enum class MotionEasing : std::uint8_t {",
        ]
    )
    lines.extend(f"    {easing['name']}," for easing in easings)
    lines.extend(
        [
            "    count,",
            "};",
            "",
            "struct CubicBezierToken {",
            "    float x1;",
            "    float y1;",
            "    float x2;",
            "    float y2;",
            "    bool is_path;",
            "};",
            "",
            "inline constexpr std::array<CubicBezierToken,",
            "    static_cast<std::size_t>(MotionEasing::count)>",
            "    kMotionEasings{{",
        ]
    )
    for easing in easings:
        points = easing.get("control_points", [0.0, 0.0, 0.0, 0.0])
        lines.append(
            "        CubicBezierToken{"
            + ", ".join(cpp_float(value) for value in points)
            + f", {str(easing['kind'] == 'path').lower()}"
            + f"}},  // {easing['upstream_name']}"
        )
    lines.extend(
        [
            "    }};",
            "",
            "}  // namespace m3e::generated",
            "",
        ]
    )
    return "\n".join(lines)


def normalized_text(data: dict[str, Any]) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def check() -> int:
    data = read_json(NORMALIZED_PATH)
    validate(data)
    expected_header = generate_header(data)
    actual_header = HEADER_PATH.read_text(encoding="utf-8")
    if actual_header != expected_header:
        print(f"generated file differs: {HEADER_PATH.relative_to(ROOT)}", file=sys.stderr)
        return 1
    print(
        "token sync check passed: "
        f"{len(data['color_roles'])} colors, "
        f"{len(data['typography_roles'])} typography roles, "
        f"{len(data['shape_roles'])} shapes"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="fetch and verify the frozen upstream token sources",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify checked-in generated artifacts without network access",
    )
    parser.add_argument(
        "--generate",
        action="store_true",
        help="regenerate C++ from checked-in normalized JSON without network access",
    )
    args = parser.parse_args()
    if sum((args.refresh, args.check, args.generate)) != 1:
        parser.error("choose exactly one of --refresh, --generate, or --check")
    if args.check:
        return check()
    if args.generate:
        data = read_json(NORMALIZED_PATH)
        validate(data)
        HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
        HEADER_PATH.write_text(generate_header(data), encoding="utf-8")
        print(f"wrote {HEADER_PATH.relative_to(ROOT)} from normalized JSON")
        return 0

    data = refresh()
    NORMALIZED_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    NORMALIZED_PATH.write_text(normalized_text(data), encoding="utf-8")
    HEADER_PATH.write_text(generate_header(data), encoding="utf-8")
    print(
        f"wrote {NORMALIZED_PATH.relative_to(ROOT)} and "
        f"{HEADER_PATH.relative_to(ROOT)} from {data['androidx_commit']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
