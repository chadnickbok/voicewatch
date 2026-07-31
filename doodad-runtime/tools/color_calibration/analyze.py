#!/usr/bin/env python3

"""Measure the CoreS3 RGB565 color-bars target from a normalized camera crop."""

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


WIDTH = 240
HEIGHT = 240
COLUMNS = 8
ROWS = 5
BORDER_X = 8
BORDER_Y = 10
CELL_WIDTH = (WIDTH - 2 * BORDER_X) // COLUMNS
CELL_HEIGHT = (HEIGHT - 2 * BORDER_Y) // ROWS
RAMP5 = (0, 4, 9, 13, 18, 22, 27, 31)
RAMP6 = (0, 9, 18, 27, 36, 45, 54, 63)


def expand5(value: int) -> int:
    return (value << 3) | (value >> 2)


def expand6(value: int) -> int:
    return (value << 2) | (value >> 4)


@dataclass(frozen=True)
class Patch:
    name: str
    row: int
    column: int
    expected: tuple[int, int, int]


def rgb565(red5: int, green6: int, blue5: int) -> tuple[int, int, int]:
    return expand5(red5), expand6(green6), expand5(blue5)


def patches() -> list[Patch]:
    bar_values = (
        ("white", rgb565(31, 63, 31)),
        ("yellow", rgb565(31, 63, 0)),
        ("cyan", rgb565(0, 63, 31)),
        ("green", rgb565(0, 63, 0)),
        ("magenta", rgb565(31, 0, 31)),
        ("red", rgb565(31, 0, 0)),
        ("blue", rgb565(0, 0, 31)),
        ("black", rgb565(0, 0, 0)),
    )
    result = [
        Patch(f"bar_{name}", 0, column, expected)
        for column, (name, expected) in enumerate(bar_values)
    ]
    for column, (five, six) in enumerate(zip(RAMP5, RAMP6, strict=True)):
        result.extend(
            (
                Patch(f"gray_{column}", 1, column, rgb565(five, six, five)),
                Patch(f"red_{column}", 2, column, rgb565(five, 0, 0)),
                Patch(f"green_{column}", 3, column, rgb565(0, six, 0)),
                Patch(f"blue_{column}", 4, column, rgb565(0, 0, five)),
            )
        )
    return result


def image_dimensions(image: Path) -> tuple[int, int]:
    completed = subprocess.run(
        ["magick", "identify", "-format", "%w %h", str(image)],
        check=True,
        capture_output=True,
        text=True,
    )
    width, height = completed.stdout.split()
    return int(width), int(height)


def sample_patch(image: Path, patch: Patch) -> tuple[int, int, int]:
    # Ignore the outer quarter of each cell so small crop/perspective errors
    # cannot mix adjacent patches into the measurement.
    sample_width = CELL_WIDTH // 2
    sample_height = CELL_HEIGHT // 2
    x = (
        BORDER_X
        + patch.column * CELL_WIDTH
        + (CELL_WIDTH - sample_width) // 2
    )
    y = (
        BORDER_Y
        + patch.row * CELL_HEIGHT
        + (CELL_HEIGHT - sample_height) // 2
    )
    pixel_format = (
        "%[fx:int(255*r+0.5)] "
        "%[fx:int(255*g+0.5)] "
        "%[fx:int(255*b+0.5)]"
    )
    completed = subprocess.run(
        [
            "magick",
            str(image),
            "-crop",
            f"{sample_width}x{sample_height}+{x}+{y}",
            "+repage",
            "-scale",
            "1x1!",
            "-colorspace",
            "sRGB",
            "-depth",
            "8",
            "-format",
            pixel_format,
            "info:",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    red, green, blue = completed.stdout.split()
    return int(red), int(green), int(blue)


def solve(matrix: list[list[float]], vector: list[float]) -> list[float]:
    size = len(vector)
    augmented = [
        [*matrix[row], vector[row]]
        for row in range(size)
    ]
    for column in range(size):
        pivot = max(
            range(column, size),
            key=lambda row: abs(augmented[row][column]),
        )
        if abs(augmented[pivot][column]) < 1e-12:
            raise ValueError("calibration samples are singular")
        augmented[column], augmented[pivot] = (
            augmented[pivot],
            augmented[column],
        )
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            augmented[row] = [
                current - factor * source
                for current, source in zip(
                    augmented[row],
                    augmented[column],
                    strict=True,
                )
            ]
    return [augmented[row][-1] for row in range(size)]


def fit_affine(
    observed: list[tuple[int, int, int]],
    expected: list[tuple[int, int, int]],
) -> list[list[float]]:
    inputs = [
        [red / 255.0, green / 255.0, blue / 255.0, 1.0]
        for red, green, blue in observed
    ]
    normal = [
        [
            sum(sample[row] * sample[column] for sample in inputs)
            for column in range(4)
        ]
        for row in range(4)
    ]
    result: list[list[float]] = []
    for channel in range(3):
        target = [sample[channel] / 255.0 for sample in expected]
        projected = [
            sum(sample[row] * value for sample, value in zip(inputs, target))
            for row in range(4)
        ]
        result.append(solve(normal, projected))
    return result


def apply_affine(
    matrix: list[list[float]],
    observed: tuple[int, int, int],
) -> tuple[float, float, float]:
    source = [
        observed[0] / 255.0,
        observed[1] / 255.0,
        observed[2] / 255.0,
        1.0,
    ]
    return tuple(
        max(0.0, min(1.0, sum(weight * value for weight, value in zip(row, source))))
        for row in matrix
    )


def rmse(
    actual: list[tuple[float, float, float]],
    expected: list[tuple[int, int, int]],
) -> float:
    squared = 0.0
    count = 0
    for actual_patch, expected_patch in zip(actual, expected, strict=True):
        for actual_channel, expected_channel in zip(
            actual_patch, expected_patch, strict=True
        ):
            squared += (actual_channel - expected_channel / 255.0) ** 2
            count += 1
    return math.sqrt(squared / count)


def write_corrected_image(
    source: Path,
    destination: Path,
    matrix: list[list[float]],
) -> None:
    decoded = subprocess.run(
        [
            "magick",
            str(source),
            "-alpha",
            "off",
            "-depth",
            "8",
            "RGB:-",
        ],
        check=True,
        capture_output=True,
    ).stdout
    expected_bytes = WIDTH * HEIGHT * 3
    if len(decoded) != expected_bytes:
        raise ValueError(
            f"expected {expected_bytes} decoded bytes, got {len(decoded)}"
        )
    corrected = bytearray(expected_bytes)
    for offset in range(0, expected_bytes, 3):
        transformed = apply_affine(
            matrix,
            (decoded[offset], decoded[offset + 1], decoded[offset + 2]),
        )
        for channel, value in enumerate(transformed):
            corrected[offset + channel] = round(value * 255.0)
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "magick",
            "-size",
            f"{WIDTH}x{HEIGHT}",
            "-depth",
            "8",
            "RGB:-",
            str(destination),
        ],
        check=True,
        input=bytes(corrected),
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Sample the 8×5 CoreS3 RGB565 target and fit an affine sRGB "
            "capture-correction matrix."
        )
    )
    parser.add_argument("image", type=Path)
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--corrected", type=Path)
    parser.add_argument("--exposure", type=int)
    parser.add_argument("--gain", type=int)
    parser.add_argument("--white-balance-temperature", type=int)
    parser.add_argument("--focus", type=int)
    parser.add_argument("--focus-mode", choices=("auto", "manual"))
    parser.add_argument("--viewport-geometry")
    parser.add_argument("--camera")
    parser.add_argument("--source-label")
    return parser.parse_args()


def main() -> int:
    options = parse_arguments()
    if shutil.which("magick") is None:
        raise SystemExit("ImageMagick is required (missing `magick`)")
    image = options.image.resolve()
    if image_dimensions(image) != (WIDTH, HEIGHT):
        raise SystemExit(f"expected a {WIDTH}x{HEIGHT} normalized crop: {image}")

    definitions = patches()
    observed = [sample_patch(image, patch) for patch in definitions]
    expected = [patch.expected for patch in definitions]
    matrix = fit_affine(observed, expected)
    observed_normalized = [
        tuple(channel / 255.0 for channel in sample)
        for sample in observed
    ]
    corrected = [apply_affine(matrix, sample) for sample in observed]
    before = rmse(observed_normalized, expected)
    after = rmse(corrected, expected)
    crushed = [
        patch.name
        for patch, sample in zip(definitions, observed, strict=True)
        if max(patch.expected) > 0 and max(sample) <= 2
    ]
    clipped = [
        patch.name
        for patch, sample in zip(definitions, observed, strict=True)
        if max(sample) >= 250
    ]
    warnings = []
    if crushed:
        warnings.append(
            f"{len(crushed)} non-black patches are crushed at or below code 2"
        )
    if clipped:
        warnings.append(
            f"{len(clipped)} patches contain a channel at or above code 250"
        )

    options.csv.parent.mkdir(parents=True, exist_ok=True)
    with options.csv.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            (
                "name",
                "row",
                "column",
                "expected_r",
                "expected_g",
                "expected_b",
                "observed_r",
                "observed_g",
                "observed_b",
            )
        )
        for patch, sample in zip(definitions, observed, strict=True):
            writer.writerow(
                (
                    patch.name,
                    patch.row,
                    patch.column,
                    *patch.expected,
                    *sample,
                )
            )

    report = {
        "schema": "doodad.color-calibration.v1",
        "source": options.source_label or str(image),
        "target": {
            "format": "RGB565",
            "width": WIDTH,
            "height": HEIGHT,
            "columns": COLUMNS,
            "rows": ROWS,
            "patches": len(definitions),
            "registration_border": {
                "horizontal_pixels": BORDER_X,
                "vertical_pixels": BORDER_Y,
            },
        },
        "capture": {
            "camera": options.camera,
            "exposure": options.exposure,
            "gain": options.gain,
            "white_balance_temperature": options.white_balance_temperature,
            "focus": options.focus,
            "focus_mode": options.focus_mode,
            "viewport_geometry": options.viewport_geometry,
        },
        "quality": {
            "observed_white": list(observed[0]),
            "observed_black": list(observed[7]),
            "shadow_crushed_patches": crushed,
            "highlight_clipped_patches": clipped,
            "warnings": warnings,
        },
        "correction": {
            "model": "expected_srgb = matrix * [observed_r, observed_g, observed_b, 1]",
            "domain": "normalized 0..1 sRGB",
            "matrix": [
                [round(value, 8) for value in row]
                for row in matrix
            ],
            "rmse_before": round(before, 8),
            "rmse_after": round(after, 8),
        },
    }
    options.json.parent.mkdir(parents=True, exist_ok=True)
    options.json.write_text(json.dumps(report, indent=2) + "\n")
    if options.corrected is not None:
        write_corrected_image(image, options.corrected, matrix)

    print(f"sampled {len(definitions)} RGB565 patches")
    print(f"RMSE before correction: {before:.4f}")
    print(f"RMSE after correction:  {after:.4f}")
    for warning in warnings:
        print(f"warning: {warning}")
    print(f"measurements: {options.csv}")
    print(f"calibration:  {options.json}")
    if options.corrected is not None:
        print(f"corrected:    {options.corrected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
