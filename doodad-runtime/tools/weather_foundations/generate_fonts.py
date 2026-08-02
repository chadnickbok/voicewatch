#!/usr/bin/env python3
"""Generate and verify the bounded Roboto bitmap fonts used by Weather."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC_PATH = ROOT / "reference/weather-foundations/weather-foundations-v1.json"
ASSET_DIR = ROOT / "components/m3e_lvgl/src/assets"
MANIFEST_PATH = ROOT / "reference/weather-foundations/generated/font-manifest.json"
DEFAULT_SOURCE = (
    ROOT
    / "reference"
    / "weather-foundations"
    / "vendor"
    / "roboto"
    / "Roboto-Medium.ttf"
)


def roles() -> list[dict[str, object]]:
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    repertoires = spec["typography"]["repertoires"]
    result = []
    for scale_milli in (1000, 1300):
        for role in spec["typography"]["roles"]:
            if scale_milli == 1300 and role["name"] == "hero":
                continue
            size_px = round(role["size_px"] * scale_milli / 1000)
            name = f"m3e_weather_{role['name']}_{size_px}"
            result.append(
                {
                    "role": role["name"],
                    "scale_milli": scale_milli,
                    "size_px": size_px,
                    "symbol": name,
                    "symbols": repertoires[role["repertoire"]],
                    "path": ASSET_DIR / f"weather_{role['name']}_{size_px}.c",
                }
            )
    return result


def expected_codepoints(symbols: str) -> set[int]:
    return {ord(value) for value in symbols if value != " "}


def file_codepoints(content: str) -> set[int]:
    return {int(value, 16) for value in re.findall(r"U\+([0-9A-Fa-f]{4,6})", content)}


def source_path() -> pathlib.Path:
    override = os.environ.get("DOODAD_ROBOTO_TTF")
    return pathlib.Path(override) if override else DEFAULT_SOURCE


def generate() -> int:
    source = source_path()
    if not source.is_file():
        print(
            f"Roboto source not found at {source}; set DOODAD_ROBOTO_TTF",
            file=sys.stderr,
        )
        return 1
    source_sha = hashlib.sha256(source.read_bytes()).hexdigest()
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    for role in roles():
        command = [
            "npx",
            "--yes",
            "lv_font_conv@1.5.3",
            "--font",
            str(source),
            "--size",
            str(role["size_px"]),
            "--bpp",
            "4",
            "--format",
            "lvgl",
            "--symbols",
            str(role["symbols"]),
            "--no-compress",
            "--lv-include",
            "lvgl.h",
            "--lv-font-name",
            str(role["symbol"]),
            "--output",
            str(role["path"]),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        path = pathlib.Path(role["path"])
        content = path.read_text(encoding="utf-8")
        notice = (
            "/*******************************************************************************\n"
            " * Doodad Weather font asset\n"
            " * Family: Roboto Medium\n"
            f" * Role: {role['role']} at {role['size_px']} physical pixels\n"
            f" * Source SHA-256: {source_sha}\n"
            " * License: Apache License 2.0\n"
            " * Generator: lv_font_conv 1.5.3, 4bpp, uncompressed\n"
            " *******************************************************************************/\n\n"
        )
        path.write_text(notice + content.rstrip() + "\n", encoding="utf-8")
    expected = {pathlib.Path(role["path"]).resolve() for role in roles()}
    for role in roles():
        for stale in ASSET_DIR.glob(f"weather_{role['role']}_*.c"):
            if stale.resolve() not in expected:
                stale.unlink()
    return write_manifest(source_sha)


def write_manifest(source_sha: str) -> int:
    entries = []
    for role in roles():
        path = pathlib.Path(role["path"])
        content = path.read_bytes()
        entries.append(
            {
                "role": role["role"],
                "scale_milli": role["scale_milli"],
                "size_px": role["size_px"],
                "symbol": role["symbol"],
                "path": str(path.relative_to(ROOT)),
                "bytes": len(content),
                "sha256": hashlib.sha256(content).hexdigest(),
                "codepoints": [f"U+{value:04X}" for value in sorted(expected_codepoints(str(role["symbols"])))],
            }
        )
    manifest = {
        "schema_version": 1,
        "family": "Roboto Medium",
        "license": "Apache-2.0",
        "source_sha256": source_sha,
        "generator": "lv_font_conv 1.5.3",
        "bpp": 4,
        "fonts": entries,
        "total_bytes": sum(item["bytes"] for item in entries),
    }
    MANIFEST_PATH.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"generated {len(entries)} Weather fonts ({manifest['total_bytes']} bytes of C source)")
    return 0


def check() -> int:
    failures = []
    if not MANIFEST_PATH.is_file():
        failures.append(f"missing {MANIFEST_PATH.relative_to(ROOT)}")
        manifest = None
    else:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    for role in roles():
        path = pathlib.Path(role["path"])
        if not path.is_file():
            failures.append(f"missing {path.relative_to(ROOT)}")
            continue
        content = path.read_text(encoding="utf-8")
        if f"Role: {role['role']} at {role['size_px']} physical pixels" not in content:
            failures.append(f"bad provenance in {path.relative_to(ROOT)}")
        if not re.search(rf"\b{re.escape(str(role['symbol']))}\s*=", content):
            failures.append(f"missing LVGL symbol {role['symbol']}")
        missing = expected_codepoints(str(role["symbols"])) - file_codepoints(content)
        if missing:
            failures.append(
                f"missing codepoints in {path.relative_to(ROOT)}: "
                + ", ".join(f"U+{value:04X}" for value in sorted(missing))
            )
        if manifest:
            entry = next(
                (
                    item
                    for item in manifest["fonts"]
                    if item["role"] == role["role"]
                    and item.get("scale_milli", 1000)
                    == role["scale_milli"]
                ),
                None,
            )
            if entry is None:
                failures.append(f"missing manifest role {role['role']}")
            elif hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                failures.append(f"font hash mismatch {path.relative_to(ROOT)}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"checked {len(roles())} Weather font assets")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--generate", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args()
    return generate() if args.generate else check()


if __name__ == "__main__":
    raise SystemExit(main())
