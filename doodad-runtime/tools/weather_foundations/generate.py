#!/usr/bin/env python3
"""Generate renderer-neutral Weather foundation assets from one canonical spec."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import pathlib
import re
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "reference/weather-foundations/weather-foundations-v1.json"
CPP_TOKENS = ROOT / "components/m3e_lvgl/include/m3e/generated/weather_tokens.hpp"
CPP_ICONS = ROOT / "components/m3e_lvgl/include/m3e/generated/weather_icons.hpp"
CPP_ICON_ASSETS_HEADER = ROOT / "components/m3e_lvgl/include/m3e/assets/weather_icon_assets.hpp"
CPP_ICON_ASSETS = ROOT / "components/m3e_lvgl/src/assets/weather_icon_assets.cpp"
C_ICON_DATA = ROOT / "components/m3e_lvgl/src/assets/weather_icon_data.c"
KOTLIN_TOKENS = ROOT / "reference/android-wear/app/src/main/java/dev/doodad/reference/ui/generated/WeatherFoundations.kt"
KOTLIN_ICONS = ROOT / "reference/android-wear/app/src/main/java/dev/doodad/reference/ui/generated/WeatherIcons.kt"
GENERATED = ROOT / "reference/weather-foundations/generated"
MANIFEST = GENERATED / "manifest.json"
ANDROID_DRAWABLES = ROOT / "reference/android-wear/app/src/main/res/drawable-nodpi"


def read_spec() -> dict[str, Any]:
    return json.loads(SOURCE.read_text(encoding="utf-8"))


def snake_to_pascal(value: str) -> str:
    return "".join(word.capitalize() for word in value.split("_"))


def enum_name(value: str) -> str:
    return value


def rgb(hex_value: str) -> tuple[int, int, int]:
    if not re.fullmatch(r"#[0-9A-Fa-f]{6}", hex_value):
        raise ValueError(f"invalid RGB color {hex_value!r}")
    return tuple(int(hex_value[index : index + 2], 16) for index in (1, 3, 5))


def rgb565_value(color: tuple[int, int, int]) -> int:
    red, green, blue = color
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def expand_rgb565(value: int) -> tuple[int, int, int]:
    red5 = (value >> 11) & 0x1F
    green6 = (value >> 5) & 0x3F
    blue5 = value & 0x1F
    return (
        (red5 << 3) | (red5 >> 2),
        (green6 << 2) | (green6 >> 4),
        (blue5 << 3) | (blue5 >> 2),
    )


def relative_luminance(color: tuple[int, int, int]) -> float:
    channels = []
    for component in color:
        value = component / 255.0
        channels.append(value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4)
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]


def contrast_ratio(first: tuple[int, int, int], second: tuple[int, int, int]) -> float:
    high, low = sorted((relative_luminance(first), relative_luminance(second)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def icon_source_path(spec: dict[str, Any], icon: dict[str, Any]) -> pathlib.Path:
    source = spec["icon_sources"][icon["source"]]
    return ROOT / source["vendor_path"] / icon["asset"]


def validate(spec: dict[str, Any]) -> None:
    if spec.get("schema_version") != 1:
        raise ValueError("unsupported Weather foundation schema")
    color_names = [item["name"] for item in spec["colors"]]
    if len(color_names) != len(set(color_names)):
        raise ValueError("duplicate Weather color role")
    for item in spec["colors"]:
        rgb(item["hex"])
    for pair in spec["contrast_pairs"]:
        if pair["foreground"] not in color_names or pair["background"] not in color_names:
            raise ValueError(f"contrast pair references unknown role: {pair}")
        colors = {item["name"]: rgb(item["hex"]) for item in spec["colors"]}
        foreground = expand_rgb565(rgb565_value(colors[pair["foreground"]]))
        background = expand_rgb565(rgb565_value(colors[pair["background"]]))
        measured = contrast_ratio(foreground, background)
        if measured + 1e-9 < pair["minimum"]:
            raise ValueError(
                f"RGB565 contrast failed for {pair['foreground']} on "
                f"{pair['background']}: {measured:.2f} < {pair['minimum']:.2f}"
            )
    shape_names = [item["name"] for item in spec["shapes"]]
    if len(shape_names) != len(set(shape_names)):
        raise ValueError("duplicate Weather shape role")
    icon_names = [item["name"] for item in spec["icons"]]
    if len(icon_names) != len(set(icon_names)):
        raise ValueError("duplicate Weather icon")
    condition_codes: list[int] = []
    source_names = set(spec["icon_sources"])
    for icon in spec["icons"]:
        if icon["source"] not in source_names:
            raise ValueError(f"icon {icon['name']} references unknown source {icon['source']}")
        if icon["render"] not in {"multicolor", "mask"}:
            raise ValueError(f"unsupported icon render mode in {icon['name']}")
        if icon["render"] == "mask" and icon.get("tint_role") not in color_names:
            raise ValueError(f"mask icon {icon['name']} needs a valid tint role")
        if icon["render"] == "multicolor" and "tint_role" in icon:
            raise ValueError(f"multicolor icon {icon['name']} cannot have a tint role")
        source_path = icon_source_path(spec, icon)
        if not source_path.is_file():
            raise ValueError(f"missing vendored icon {source_path.relative_to(ROOT)}")
        if source_path.suffix != ".svg":
            raise ValueError(f"weather icon source must be SVG: {source_path}")
        if "condition_code" in icon:
            condition_codes.append(icon["condition_code"])
    if sorted(condition_codes) != list(range(16)):
        raise ValueError(f"condition icon codes must be exactly 0..15, got {sorted(condition_codes)}")
    for role in spec["typography"]["roles"]:
        if role["repertoire"] not in spec["typography"]["repertoires"]:
            raise ValueError(f"unknown font repertoire {role['repertoire']}")


def svg_for_icon(spec: dict[str, Any], icon: dict[str, Any]) -> str:
    value = icon_source_path(spec, icon).read_text(encoding="utf-8").strip()
    label = icon["name"].replace("_", " ")
    extra = f' role="img" aria-label="{label}" data-source="{icon["source"]}"'
    if icon["render"] == "mask":
        colors = {item["name"]: item["hex"] for item in spec["colors"]}
        extra += f' fill="{colors[icon["tint_role"]]}" data-tint-role="{icon["tint_role"]}"'
    value = value.replace("<svg ", f"<svg{extra} ", 1)
    return value + "\n"


def png_for_svg(svg: bytes, size: int) -> bytes:
    try:
        result = subprocess.run(
            ["rsvg-convert", "--width", str(size), "--height", str(size)],
            input=svg,
            check=True,
            capture_output=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError("rsvg-convert is required to generate Weather PNG assets") from error
    return result.stdout


LVGL_ICON_SIZES = (18, 24, 32, 64)


def rgba_for_png(png: bytes, size: int) -> bytes:
    try:
        result = subprocess.run(
            ["magick", "png:-", "rgba:-"],
            input=png,
            check=True,
            capture_output=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError("ImageMagick 7 is required to generate LVGL Weather assets") from error
    if len(result.stdout) != size * size * 4:
        raise RuntimeError(f"unexpected {size}px Weather raster size")
    return result.stdout


def c_bytes(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]) + ",")
    return "\n".join(rows)


def lvgl_icon_data(spec: dict[str, Any], svgs: dict[str, bytes]) -> str:
    chunks = [
        "// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.",
        '#include "lvgl.h"',
        "",
    ]
    for icon in spec["icons"]:
        for size in LVGL_ICON_SIZES:
            # Rasterize every shipping size from the canonical vector. Going
            # through the 64 px review PNG softens the 18/24/32 px assets and
            # makes small utility glyphs visibly less crisp than Compose.
            rgba = rgba_for_png(
                png_for_svg(svgs[icon["name"]], size),
                size,
            )
            if icon["render"] == "multicolor":
                color = bytearray()
                alpha = bytearray()
                for offset in range(0, len(rgba), 4):
                    red, green, blue, opacity = rgba[offset : offset + 4]
                    value = rgb565_value((red, green, blue))
                    color.extend((value & 0xff, value >> 8))
                    alpha.append(opacity)
                payload = bytes(color + alpha)
                color_format = "LV_COLOR_FORMAT_RGB565A8"
                stride = size * 2
            else:
                mask = bytearray(
                    rgba[offset + 3]
                    for offset in range(0, len(rgba), 4)
                )
                payload = bytes(mask)
                color_format = "LV_COLOR_FORMAT_A8"
                stride = size
            symbol = f"m3e_weather_icon_{icon['name']}_{size}"
            chunks.extend((
            f"static const uint8_t {symbol}_data[] = {{",
            c_bytes(payload),
            "};",
            f"const lv_image_dsc_t {symbol} = {{",
            "    .header = {",
            "        .magic = LV_IMAGE_HEADER_MAGIC,",
            f"        .cf = {color_format},",
            "        .flags = 0,",
            f"        .w = {size},",
            f"        .h = {size},",
            f"        .stride = {stride},",
            "        .reserved_2 = 0,",
            "    },",
            f"    .data_size = sizeof({symbol}_data),",
            f"    .data = {symbol}_data,",
            "    .reserved = NULL,",
            "    .reserved_2 = NULL,",
            "};",
            "",
            ))
    return "\n".join(chunks)


def cpp_icon_assets_header() -> str:
    return '''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
#pragma once

#include "lvgl.h"
#include "m3e/generated/weather_icons.hpp"

#include <cstdint>

namespace m3e {

const lv_image_dsc_t* weather_icon_asset(
    generated::WeatherIcon icon,
    std::int32_t size);

}  // namespace m3e
'''


def cpp_icon_assets(spec: dict[str, Any]) -> str:
    declarations = "\n".join(
        f"extern const lv_image_dsc_t m3e_weather_icon_{icon['name']}_{size};"
        for icon in spec["icons"] for size in LVGL_ICON_SIZES
    )
    pointers = "\n".join(
        "    {{" + ", ".join(
            f"&m3e_weather_icon_{icon['name']}_{size}"
            for size in LVGL_ICON_SIZES
        ) + "}},"
        for icon in spec["icons"]
    )
    return f'''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
#include "m3e/assets/weather_icon_assets.hpp"

#include <array>
#include <cstddef>

extern "C" {{
{declarations}
}}

namespace m3e {{

const lv_image_dsc_t* weather_icon_asset(
    generated::WeatherIcon icon,
    std::int32_t size) {{
    static constexpr std::array<std::array<const lv_image_dsc_t*, 4>,
        static_cast<std::size_t>(generated::WeatherIcon::count)> kAssets{{{{
{pointers}
    }}}};
    const auto index = static_cast<std::size_t>(icon);
    if (index >= kAssets.size()) return nullptr;
    const auto size_index = size <= 18 ? 0U : size <= 24 ? 1U : size <= 32 ? 2U : 3U;
    return kAssets[index][size_index];
}}

}}  // namespace m3e
'''


def cpp_tokens(spec: dict[str, Any]) -> str:
    color_rows = []
    for item in spec["colors"]:
        red, green, blue = rgb(item["hex"])
        color_rows.append(
            f'    WeatherColorToken{{WeatherRgb888{{{red}, {green}, {blue}}}, 0x{rgb565_value((red, green, blue)):04X}}},  // {item["name"]}'
        )
    type_rows = [
        f'    WeatherTypographyToken{{{role["size_px"]}, {role["line_height_px"]}, {role["weight"]}}},  // {role["name"]}'
        for role in spec["typography"]["roles"]
    ]
    shape_kind = {"full": "full", "rounded": "rounded", "cut_corners": "cut_corners"}
    shape_rows = []
    for shape in spec["shapes"]:
        corners = ", ".join(str(value) for value in shape["corners_dp"])
        shape_rows.append(
            f'    WeatherShapeToken{{WeatherShapeKind::{shape_kind[shape["kind"]]}, {{{corners}}}, {shape["inset_dp"]}}},  // {shape["name"]}'
        )
    color_enum = ",\n    ".join(enum_name(item["name"]) for item in spec["colors"])
    type_enum = ",\n    ".join(enum_name(item["name"]) for item in spec["typography"]["roles"])
    shape_enum = ",\n    ".join(enum_name(item["name"]) for item in spec["shapes"])
    return f'''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
// Source: reference/weather-foundations/weather-foundations-v1.json
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e::generated {{

struct WeatherRgb888 {{ std::uint8_t red; std::uint8_t green; std::uint8_t blue; }};
struct WeatherColorToken {{ WeatherRgb888 rgb888; std::uint16_t rgb565; }};

enum class WeatherColorRole : std::uint8_t {{
    {color_enum},
    count,
}};

inline constexpr std::array<WeatherColorToken, static_cast<std::size_t>(WeatherColorRole::count)>
    kWeatherColors{{{{
{chr(10).join(color_rows)}
    }}}};

enum class WeatherTypographyRole : std::uint8_t {{
    {type_enum},
    count,
}};

struct WeatherTypographyToken {{
    std::uint8_t size_px;
    std::uint8_t line_height_px;
    std::uint16_t weight;
}};

inline constexpr std::array<WeatherTypographyToken, static_cast<std::size_t>(WeatherTypographyRole::count)>
    kWeatherTypography{{{{
{chr(10).join(type_rows)}
    }}}};

enum class WeatherShapeKind : std::uint8_t {{ rounded, full, cut_corners }};
enum class WeatherShapeRole : std::uint8_t {{
    {shape_enum},
    count,
}};

struct WeatherShapeToken {{
    WeatherShapeKind kind;
    std::array<std::uint8_t, 4> corners_dp;  // top-left, top-right, bottom-right, bottom-left
    std::uint8_t inset_dp;
}};

inline constexpr std::array<WeatherShapeToken, static_cast<std::size_t>(WeatherShapeRole::count)>
    kWeatherShapes{{{{
{chr(10).join(shape_rows)}
    }}}};

}}  // namespace m3e::generated
'''


def cpp_icons(spec: dict[str, Any]) -> str:
    icon_names = [item["name"] for item in spec["icons"]]
    source_names = list(spec["icon_sources"])
    rows = []
    for icon in spec["icons"]:
        tint_role = icon.get("tint_role", "background")
        rows.append(
            "    WeatherIconSpec{"
            + f"{icon.get('condition_code', -1)}, "
            + f"WeatherIconSource::{icon['source']}, "
            + f"WeatherIconRender::{icon['render']}, "
            + f"{str('tint_role' in icon).lower()}, "
            + f"WeatherColorRole::{tint_role}, "
            + f'"{pathlib.Path(icon["asset"]).stem}"'
            + f"}},  // {icon['name']}"
        )
    return f'''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
// Renderer-neutral catalog for vendored Weather icon assets.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "m3e/generated/weather_tokens.hpp"

namespace m3e::generated {{

enum class WeatherIconSource : std::uint8_t {{ {", ".join(source_names)} }};
enum class WeatherIconRender : std::uint8_t {{ multicolor, mask }};
enum class WeatherIcon : std::uint8_t {{ {", ".join(icon_names)}, count }};

struct WeatherIconSpec {{
    std::int8_t condition_code;
    WeatherIconSource source;
    WeatherIconRender render;
    bool has_tint;
    WeatherColorRole tint_role;
    std::string_view asset_stem;
}};

inline constexpr std::array<WeatherIconSpec, static_cast<std::size_t>(WeatherIcon::count)>
    kWeatherIcons{{{{
{chr(10).join(rows)}
    }}}};

inline constexpr std::array<std::string_view, static_cast<std::size_t>(WeatherIcon::count)>
    kWeatherIconWireNames{{{{
{chr(10).join(f'    "{name}",' for name in icon_names)}
    }}}};

}}  // namespace m3e::generated
'''
def kotlin_tokens(spec: dict[str, Any]) -> str:
    colors = []
    for item in spec["colors"]:
        red, green, blue = rgb(item["hex"])
        colors.append(
            f'        WeatherColorRole.{snake_to_pascal(item["name"])} to WeatherColorToken(0x{red:02X}{green:02X}{blue:02X}, 0x{rgb565_value((red, green, blue)):04X}),'
        )
    type_rows = [
        f'        WeatherTypographyRole.{snake_to_pascal(role["name"])} to WeatherTypographyToken({role["size_px"]}, {role["line_height_px"]}, {role["weight"]}),'
        for role in spec["typography"]["roles"]
    ]
    shape_rows = [
        f'        WeatherShapeRole.{snake_to_pascal(shape["name"])} to WeatherShapeToken(WeatherShapeKind.{snake_to_pascal(shape["kind"])}, intArrayOf({", ".join(map(str, shape["corners_dp"]))}), {shape["inset_dp"]}),'
        for shape in spec["shapes"]
    ]
    return f'''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
package dev.doodad.reference.ui.generated

enum class WeatherColorRole {{ {", ".join(snake_to_pascal(item["name"]) for item in spec["colors"])} }}
data class WeatherColorToken(val rgb888: Int, val rgb565: Int)
enum class WeatherTypographyRole {{ {", ".join(snake_to_pascal(item["name"]) for item in spec["typography"]["roles"])} }}
data class WeatherTypographyToken(val sizePx: Int, val lineHeightPx: Int, val weight: Int)
enum class WeatherShapeKind {{ Rounded, Full, CutCorners }}
enum class WeatherShapeRole {{ {", ".join(snake_to_pascal(item["name"]) for item in spec["shapes"])} }}
data class WeatherShapeToken(val kind: WeatherShapeKind, val cornersDp: IntArray, val insetDp: Int)

object WeatherFoundations {{
    const val fontFamily = "Roboto"
    const val fallbackFontFamily = "Montserrat"
    val colors = mapOf(
{chr(10).join(colors)}
    )
    val typography = mapOf(
{chr(10).join(type_rows)}
    )
    val shapes = mapOf(
{chr(10).join(shape_rows)}
    )
}}
'''


def kotlin_icons(spec: dict[str, Any]) -> str:
    icon_names = [item["name"] for item in spec["icons"]]
    source_names = list(spec["icon_sources"])
    rows = []
    for icon in spec["icons"]:
        tint = (
            f"WeatherColorRole.{snake_to_pascal(icon['tint_role'])}"
            if "tint_role" in icon
            else "null"
        )
        rows.append(
            f"        WeatherIcon.{snake_to_pascal(icon['name'])} to WeatherIconSpec("
            + f"R.drawable.weather_icon_{icon['name']}, "
            + f"WeatherIconSource.{snake_to_pascal(icon['source'])}, "
            + f"WeatherIconRender.{snake_to_pascal(icon['render'])}, "
            + f"{icon.get('condition_code', -1)}, {tint}),"
        )
    return f'''// Generated by tools/weather_foundations/generate.py. DO NOT EDIT.
package dev.doodad.reference.ui.generated

import dev.doodad.reference.R

enum class WeatherIconSource {{ {", ".join(snake_to_pascal(name) for name in source_names)} }}
enum class WeatherIconRender {{ Multicolor, Mask }}
enum class WeatherIcon(val wireName: String) {{
{chr(10).join(f'    {snake_to_pascal(name)}("{name}"),' for name in icon_names)}
}}
data class WeatherIconSpec(
    val drawableRes: Int,
    val source: WeatherIconSource,
    val render: WeatherIconRender,
    val conditionCode: Int,
    val tintRole: WeatherColorRole?,
)

object WeatherIcons {{
    fun fromWireName(name: String): WeatherIcon =
        WeatherIcon.entries.singleOrNull {{ it.wireName == name }}
            ?: error("Unknown Weather icon $name")

    val icons = mapOf(
{chr(10).join(rows)}
    )
}}
'''
def contrast_report(spec: dict[str, Any]) -> str:
    colors = {item["name"]: rgb(item["hex"]) for item in spec["colors"]}
    lines = [
        "# Weather RGB565 contrast report",
        "",
        "Generated from `weather-foundations-v1.json`. Ratios use colors expanded back from RGB565, matching the display path.",
        "",
        "| Foreground | Background | Required | RGB888 | RGB565 | Result |",
        "| --- | --- | ---: | ---: | ---: | --- |",
    ]
    for pair in spec["contrast_pairs"]:
        foreground = colors[pair["foreground"]]
        background = colors[pair["background"]]
        source_ratio = contrast_ratio(foreground, background)
        quantized_ratio = contrast_ratio(
            expand_rgb565(rgb565_value(foreground)), expand_rgb565(rgb565_value(background))
        )
        passed = quantized_ratio + 1e-9 >= pair["minimum"]
        lines.append(
            f'| `{pair["foreground"]}` | `{pair["background"]}` | {pair["minimum"]:.1f}:1 | '
            f'{source_ratio:.2f}:1 | {quantized_ratio:.2f}:1 | {"PASS" if passed else "FAIL"} |'
        )
    lines.extend(("", "All required pairs must pass after RGB565 quantization.", ""))
    return "\n".join(lines)


def gallery_html(spec: dict[str, Any]) -> str:
    colors = "".join(
        f'<div class="swatch"><i style="background:{item["hex"]}"></i><code>{item["name"]}</code><span>{item["hex"]}</span></div>'
        for item in spec["colors"]
    )
    icons = "".join(
        f'<figure><img src="icons/{item["name"]}.svg" alt=""><figcaption>'
        f'{item["name"].replace("_", " ")}<small>{item["source"].replace("_", " ")}</small>'
        f'</figcaption></figure>'
        for item in spec["icons"]
    )
    shapes = "".join(
        f'<figure><div class="shape {item["kind"]}" style="--tl:{item["corners_dp"][0]}px;--tr:{item["corners_dp"][1]}px;--br:{item["corners_dp"][2]}px;--bl:{item["corners_dp"][3]}px"></div><figcaption>{item["name"].replace("_", " ")}</figcaption></figure>'
        for item in spec["shapes"]
    )
    specimens = "".join(
        f'<div class="type-row"><code>{role["name"]} · {role["size_px"]}px</code><span style="font-size:{role["size_px"]}px;line-height:{role["line_height_px"]}px">{html.escape("62° Partly cloudy")}</span></div>'
        for role in spec["typography"]["roles"]
    )
    return f'''<!doctype html>
<meta charset="utf-8">
<title>Weather foundations v1</title>
<style>
*{{box-sizing:border-box}} body{{margin:0;padding:28px;background:#020817;color:#f7f8ff;font:14px Roboto,Arial,sans-serif}} h1{{margin:0 0 6px;font-size:28px}} h2{{margin:28px 0 12px;font-size:18px;color:#d7e4ff}} p{{margin:0;color:#aebfe2}} .grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(112px,1fr));gap:10px}} figure{{margin:0;padding:10px;border-radius:16px;background:#0d2342;text-align:center}} figure img{{width:64px;height:64px}} figcaption{{margin-top:7px;color:#d7e4ff;font-size:10px}} figcaption small{{display:block;margin-top:3px;color:#788bad;font-size:8px}} .swatches{{display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:8px}} .swatch{{display:grid;grid-template-columns:36px 1fr auto;align-items:center;gap:8px;padding:7px;border-radius:12px;background:#0d2342}} .swatch i{{width:36px;height:28px;border-radius:8px;border:1px solid #31527e}} .swatch span{{color:#aebfe2;font-size:10px}} code{{color:#d7e4ff;font-size:11px}} .shapes{{grid-template-columns:repeat(5,1fr)}} .shape{{height:58px;background:#315f9c;border-radius:var(--tl) var(--tr) var(--br) var(--bl)}} .shape.full{{border-radius:999px}} .shape.cut_corners{{clip-path:polygon(12% 0,88% 0,100% 18%,100% 82%,88% 100%,12% 100%,0 82%,0 18%)}} .types{{display:grid;gap:7px}} .type-row{{display:grid;grid-template-columns:130px 1fr;align-items:baseline;min-height:42px;padding:8px 12px;border-radius:12px;background:#0d2342;overflow:hidden}} .type-row span{{white-space:nowrap}} footer{{margin-top:28px;color:#7fd88b}}
</style>
<h1>Weather foundations v1</h1>
<p>Meteocons Flat conditions · Material Symbols Rounded utilities · Roboto · RGB565-checked palette</p>
<h2>Condition and utility icons</h2><div class="grid">{icons}</div>
<h2>Weather color roles</h2><div class="swatches">{colors}</div>
<h2>Square shape roles</h2><div class="grid shapes">{shapes}</div>
<h2>Roboto typography roles</h2><div class="types">{specimens}</div>
<footer>Canonical source: reference/weather-foundations/weather-foundations-v1.json</footer>
'''


def outputs(spec: dict[str, Any]) -> dict[pathlib.Path, bytes]:
    generated: dict[pathlib.Path, bytes] = {
        CPP_TOKENS: cpp_tokens(spec).encode(),
        CPP_ICONS: cpp_icons(spec).encode(),
        CPP_ICON_ASSETS_HEADER: cpp_icon_assets_header().encode(),
        CPP_ICON_ASSETS: cpp_icon_assets(spec).encode(),
        KOTLIN_TOKENS: kotlin_tokens(spec).encode(),
        KOTLIN_ICONS: kotlin_icons(spec).encode(),
        GENERATED / "contrast-report.md": contrast_report(spec).encode(),
        GENERATED / "foundation-gallery.html": gallery_html(spec).encode(),
    }
    svgs: dict[str, bytes] = {}
    for icon in spec["icons"]:
        svg = svg_for_icon(spec, icon).encode()
        png64 = png_for_svg(svg, 64)
        svgs[icon["name"]] = svg
        generated[GENERATED / "icons" / f"{icon['name']}.svg"] = svg
        generated[GENERATED / "raster-64" / f"{icon['name']}.png"] = png64
        generated[ANDROID_DRAWABLES / f"weather_icon_{icon['name']}.png"] = png_for_svg(svg, 128)
    generated[C_ICON_DATA] = lvgl_icon_data(spec, svgs).encode()
    manifest = {
        "schema_version": 1,
        "source": str(SOURCE.relative_to(ROOT)),
        "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "vendor_sources": {
            name: {
                "package": source["package"],
                "version": source["version"],
                "license": source["license"],
                "archive_sha256": source["archive_sha256"],
            }
            for name, source in spec["icon_sources"].items()
        },
        "outputs": {
            str(path.relative_to(ROOT)): hashlib.sha256(content).hexdigest()
            for path, content in sorted(generated.items(), key=lambda item: str(item[0]))
        },
    }
    generated[MANIFEST] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    return generated


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    spec = read_spec()
    validate(spec)
    expected = outputs(spec)
    if args.generate:
        for path, content in expected.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        print(f"generated {len(expected)} Weather foundation files")
        return 0
    failures = []
    for path, content in expected.items():
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT)}")
        elif path.read_bytes() != content:
            failures.append(f"stale {path.relative_to(ROOT)}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"checked {len(expected)} Weather foundation files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
