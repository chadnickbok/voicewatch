#!/usr/bin/env python3

"""Apply a checked CoreS3 camera calibration to a normalized 240×240 crop."""

from __future__ import annotations

import argparse
import json
import math
import shutil
from pathlib import Path
from typing import Any

from analyze import WIDTH, HEIGHT, image_dimensions, write_corrected_image


def load_profile(path: Path) -> dict[str, Any]:
    try:
        profile = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"could not read calibration profile {path}: {error}") from error

    if profile.get("schema") != "doodad.color-calibration.v1":
        raise ValueError("unsupported calibration profile schema")
    capture = profile.get("capture")
    if not isinstance(capture, dict):
        raise ValueError("calibration profile has no capture metadata")
    correction = profile.get("correction")
    if not isinstance(correction, dict):
        raise ValueError("calibration profile has no correction model")
    matrix = correction.get("matrix")
    if (
        not isinstance(matrix, list)
        or len(matrix) != 3
        or any(not isinstance(row, list) or len(row) != 4 for row in matrix)
        or any(
            not isinstance(value, (int, float)) or not math.isfinite(value)
            for row in matrix
            for value in row
        )
    ):
        raise ValueError("calibration correction matrix must be finite and 3×4")
    return profile


def require_capture_setting(
    profile: dict[str, Any],
    key: str,
    requested: int | str | None,
    label: str,
) -> None:
    if requested is None:
        return
    calibrated = profile["capture"].get(key)
    if calibrated != requested:
        raise ValueError(
            f"{label} mismatch: capture uses {requested}, "
            f"but calibration requires {calibrated}"
        )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Color-correct a normalized CoreS3 hardware capture."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--exposure", type=int)
    parser.add_argument("--gain", type=int)
    parser.add_argument("--white-balance-temperature", type=int)
    parser.add_argument("--focus", type=int)
    parser.add_argument("--focus-mode", choices=("auto", "manual"))
    return parser.parse_args()


def main() -> int:
    options = parse_arguments()
    if shutil.which("magick") is None:
        raise SystemExit("ImageMagick is required (missing `magick`)")
    try:
        profile = load_profile(options.profile.resolve())
        require_capture_setting(
            profile, "exposure", options.exposure, "exposure"
        )
        require_capture_setting(profile, "gain", options.gain, "gain")
        require_capture_setting(
            profile,
            "white_balance_temperature",
            options.white_balance_temperature,
            "white-balance temperature",
        )
        require_capture_setting(profile, "focus", options.focus, "focus")
        require_capture_setting(
            profile, "focus_mode", options.focus_mode, "focus mode"
        )
    except ValueError as error:
        raise SystemExit(str(error)) from error

    source = options.input.resolve()
    if image_dimensions(source) != (WIDTH, HEIGHT):
        raise SystemExit(f"expected a {WIDTH}x{HEIGHT} normalized crop: {source}")
    write_corrected_image(
        source,
        options.output.resolve(),
        profile["correction"]["matrix"],
    )
    print(f"corrected: {options.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
