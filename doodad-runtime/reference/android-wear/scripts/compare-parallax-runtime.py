#!/usr/bin/env python3
"""Compare native Wear runtime captures with host Compose captures."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "tools"))

from doodad_cli.parallax_compare import compare_rgb888_images  # noqa: E402


WIDTH = 240
HEIGHT = 240
RGB888_BYTES = WIDTH * HEIGHT * 3


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--runtime",
        type=Path,
        required=True,
        help="Directory produced by capture-parallax-suite.sh",
    )
    parser.add_argument(
        "--host",
        type=Path,
        required=True,
        help="Project Parallax perfect-render output root",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="JSON comparison report to write",
    )
    return parser.parse_args()


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise SystemExit(f"Expected a JSON object: {path}")
    return value


def sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def runtime_rgb888(path: Path) -> bytes:
    try:
        result = subprocess.run(
            [
                "magick",
                str(path),
                "-alpha",
                "off",
                "-depth",
                "8",
                "rgb:-",
            ],
            check=True,
            capture_output=True,
        )
    except FileNotFoundError as error:
        raise SystemExit(
            "ImageMagick 7 is required to decode Android runtime PNGs"
        ) from error
    if len(result.stdout) != RGB888_BYTES:
        raise SystemExit(
            f"{path} decoded to {len(result.stdout)} bytes; "
            f"expected {RGB888_BYTES}"
        )
    return result.stdout


def main() -> int:
    args = parse_args()
    runtime_root = args.runtime.resolve()
    host_root = args.host.resolve()
    manifest_paths = sorted(
        runtime_root.glob(
            "*.resting.watch_square_240.runtime-manifest.json"
        )
    )
    if not manifest_paths:
        raise SystemExit(f"No runtime manifests found in {runtime_root}")

    cases = []
    total_pixels = 0
    total_changed = 0
    total_absolute_error = 0
    total_squared_error = 0
    maximum_channel_error = 0
    runtime_fingerprints = set()
    runtime_builds = set()
    host_builds = set()

    for runtime_manifest_path in manifest_paths:
        runtime_manifest = read_json(runtime_manifest_path)
        selection = runtime_manifest["selection"]
        slug = selection["app_slug"]
        if selection["profile_id"] != "watch_square_240":
            raise SystemExit(f"{slug}: unexpected runtime profile")

        runtime_screenshot = (
            runtime_root
            / runtime_manifest["artifacts"]["screenshot"]["path"]
        )
        runtime_png = runtime_screenshot.read_bytes()
        expected_runtime_hash = runtime_manifest["artifacts"]["screenshot"][
            "sha256"
        ]
        if sha256(runtime_png) != expected_runtime_hash:
            raise SystemExit(f"{slug}: runtime screenshot hash mismatch")

        host_case = (
            host_root
            / slug
            / "resting"
            / "watch_square_240"
            / "sequence-0000"
        )
        host_manifest = read_json(host_case / "compose-manifest.json")
        if (
            host_manifest["selection"]["snapshot_sha256"]
            != selection["snapshot_sha256"]
        ):
            raise SystemExit(f"{slug}: host/runtime snapshot mismatch")
        host_rgb_path = (
            host_case / host_manifest["artifacts"]["rgb888"]["path"]
        )
        host_rgb = host_rgb_path.read_bytes()
        if len(host_rgb) != RGB888_BYTES:
            raise SystemExit(f"{slug}: host RGB888 size mismatch")
        if (
            sha256(host_rgb)
            != host_manifest["artifacts"]["rgb888"]["sha256"]
        ):
            raise SystemExit(f"{slug}: host RGB888 hash mismatch")

        comparison = compare_rgb888_images(
            host_rgb,
            runtime_rgb888(runtime_screenshot),
            width=WIDTH,
            height=HEIGHT,
        )
        cases.append(
            {
                "app_slug": slug,
                "snapshot_sha256": selection["snapshot_sha256"],
                "pixel": comparison.to_dict(),
                "artifacts": {
                    "host_rgb888": str(host_rgb_path),
                    "runtime_png": str(runtime_screenshot),
                    "runtime_accessibility_xml": str(
                        runtime_root
                        / runtime_manifest["artifacts"][
                            "accessibility_xml"
                        ]["path"]
                    ),
                },
            }
        )
        total_pixels += comparison.pixel_count
        total_changed += comparison.changed_pixels
        total_absolute_error += comparison.absolute_error_sum
        total_squared_error += comparison.squared_error_sum
        maximum_channel_error = max(
            maximum_channel_error,
            comparison.max_channel_error,
        )
        runtime_fingerprints.add(
            runtime_manifest["device"]["build_fingerprint"]
        )
        runtime_builds.add(runtime_manifest["renderer"]["build_sha256"])
        host_builds.add(host_manifest["renderer"]["build_sha256"])

    channel_samples = total_pixels * 3
    report = {
        "schema_version": 1,
        "kind": "parallax-compose-host-runtime-comparison",
        "profile_id": "watch_square_240",
        "summary": {
            "case_count": len(cases),
            "snapshot_attested_count": len(cases),
            "pixel_count": total_pixels,
            "channel_samples": channel_samples,
            "changed_pixels": total_changed,
            "changed_pixel_fraction": total_changed / total_pixels,
            "absolute_error_sum": total_absolute_error,
            "squared_error_sum": total_squared_error,
            "max_channel_error": maximum_channel_error,
            "mae": total_absolute_error / channel_samples,
            "rmse": math.sqrt(total_squared_error / channel_samples),
        },
        "environment": {
            "runtime_build_fingerprints": sorted(runtime_fingerprints),
            "runtime_renderer_build_sha256": sorted(runtime_builds),
            "host_renderer_build_sha256": sorted(host_builds),
        },
        "cases": cases,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(
            report,
            ensure_ascii=False,
            allow_nan=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )
    summary = report["summary"]
    print(
        f"{summary['case_count']} host/runtime pairs: "
        f"{summary['changed_pixels']}/{summary['pixel_count']} "
        f"pixels changed "
        f"({summary['changed_pixel_fraction']:.2%}), "
        f"RMSE {summary['rmse']:.2f}"
    )
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
