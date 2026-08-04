#!/usr/bin/env python3
"""Two-panel webcam calibration, evidence generation, and review records."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import select
import shutil
import subprocess
import tempfile
import termios
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCHEMA = "doodad.capture-profile.v2"
BOARDS = ("cores3", "t-watch-s3")
VISUAL_RE = re.compile(
    rb"\[visual\] device_id=([^ ]+) scene=([^ ]+) revision=(\d+) frame_hash=([0-9a-fA-F]+)"
)


def run(*args: str, capture: bool = False) -> str:
    result = subprocess.run(args, check=True, capture_output=capture, text=capture)
    return result.stdout if capture else ""


def dimensions(image: Path) -> tuple[int, int]:
    value = run("magick", "identify", "-format", "%w %h", str(image), capture=True)
    width, height = value.split()
    return int(width), int(height)


def image_stat(image: Path, expression: str) -> float:
    value = run(
        "magick", str(image), "-colorspace", "Gray", "-format", expression,
        "info:", capture=True,
    )
    return float(value)


def quad_from_box(x: int, y: int, width: int, height: int) -> list[list[float]]:
    return [[x, y], [x + width - 1, y], [x + width - 1, y + height - 1], [x, y + height - 1]]


def detect_panel_quads(image: Path) -> list[list[list[float]]]:
    completed = subprocess.run(
        [
            "magick", str(image), "-colorspace", "Gray", "-threshold", "62%",
            "-define", "connected-components:verbose=true",
            "-connected-components", "8", "null:",
        ],
        check=True, capture_output=True, text=True,
    )
    output = completed.stdout + completed.stderr
    pattern = re.compile(
        r"^\s*\d+:\s+(\d+)x(\d+)\+(\d+)\+(\d+)\s+[^ ]+\s+([0-9.e+]+)\s+gray\(255\)$",
        re.MULTILINE,
    )
    candidates: list[tuple[float, list[list[float]]]] = []
    image_width, image_height = dimensions(image)
    for match in pattern.finditer(output):
        width, height, x, y = map(int, match.group(1, 2, 3, 4))
        area = float(match.group(5))
        box_area = width * height
        ratio = width / max(1, height)
        fill = area / max(1, box_area)
        if (
            min(width, height) >= min(image_width, image_height) * 0.12
            and max(width, height) <= max(image_width, image_height) * 0.75
            and 0.68 <= ratio <= 1.35
            and 0.015 <= fill <= 0.48
        ):
            square_score = abs(math.log(ratio)) + abs(fill - 0.10)
            candidates.append((square_score, quad_from_box(x, y, width, height)))
    candidates.sort(key=lambda item: item[0])
    chosen: list[list[list[float]]] = []
    for _, quad in candidates:
        center = ((quad[0][0] + quad[2][0]) / 2, (quad[0][1] + quad[2][1]) / 2)
        if all(
            math.dist(center, ((other[0][0] + other[2][0]) / 2, (other[0][1] + other[2][1]) / 2)) > 80
            for other in chosen
        ):
            chosen.append(quad)
        if len(chosen) == 2:
            break
    if len(chosen) != 2:
        raise ValueError(f"expected two registered displays, detected {len(chosen)}")
    return chosen


def perspective_crop(source: Path, output: Path, quad: list[list[float]]) -> None:
    destination = ((0, 0), (239, 0), (239, 239), (0, 239))
    points = " ".join(
        f"{source_point[0]},{source_point[1]} {target[0]},{target[1]}"
        for source_point, target in zip(quad, destination, strict=True)
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    run(
        "magick", str(source), "-alpha", "off", "-virtual-pixel", "black",
        "-define", "distort:viewport=240x240+0+0", "-distort", "Perspective",
        points, str(output),
    )


def marker_score(crop: Path, x: int) -> float:
    value = run(
        "magick", str(crop), "-crop", f"4x4+{x}+12", "+repage",
        "-colorspace", "Gray", "-format", "%[fx:mean]", "info:", capture=True,
    )
    return float(value)


def label_panels(raw: Path, quads: list[list[list[float]]], directory: Path) -> dict[str, list[list[float]]]:
    observations: list[tuple[float, float, list[list[float]]]] = []
    for index, quad in enumerate(quads):
        crop = directory / f"candidate-{index}.png"
        perspective_crop(raw, crop, quad)
        observations.append((marker_score(crop, 207), marker_score(crop, 214), quad))
    watch = [item for item in observations if item[0] > 0.55 and item[1] > 0.55]
    core = [item for item in observations if item[0] > 0.55 and item[1] < 0.40]
    if len(watch) != 1 or len(core) != 1:
        raise ValueError(f"registration markers are missing or ambiguous: {observations[:2]}")
    return {"cores3": core[0][2], "t-watch-s3": watch[0][2]}


def quality(crop: Path) -> dict[str, float]:
    mean = image_stat(crop, "%[fx:mean]")
    deviation = image_stat(crop, "%[fx:standard_deviation]")
    with tempfile.TemporaryDirectory() as temporary:
        edge = Path(temporary) / "edge.png"
        run("magick", str(crop), "-colorspace", "Gray", "-edge", "1", str(edge))
        edge_mean = image_stat(edge, "%[fx:mean]")
    if deviation < 0.08:
        raise ValueError(f"display crop is blank or unreadable (deviation={deviation:.4f})")
    if mean > 0.92:
        raise ValueError(f"display crop is overexposed (mean={mean:.4f})")
    if edge_mean < 0.004:
        raise ValueError(f"display crop is too blurred (edge mean={edge_mean:.4f})")
    return {"mean": mean, "standard_deviation": deviation, "edge_mean": edge_mean}


def homography(quad: list[list[float]]) -> list[list[float]]:
    # Stored as an explicit source quadrilateral plus destination. ImageMagick
    # computes the actual projective coefficients at capture time.
    return [[*point, *destination] for point, destination in zip(
        quad, ((0, 0), (239, 0), (239, 239), (0, 239)), strict=True
    )]


def calibrate(options: argparse.Namespace) -> int:
    raw = options.raw.resolve()
    if not options.offline:
        wait_for_visual(
            {"cores3": options.cores3_port, "t-watch-s3": options.watch_port},
            "catalog-33", options.timeout,
        )
    if not raw.exists():
        if options.cleancam is None:
            raise ValueError("registration frame is missing and --cleancam was not supplied")
        raw.parent.mkdir(parents=True, exist_ok=True)
        run(
            str(options.cleancam), "--capture", str(raw),
            "--exposure", str(options.exposure), "--gain", str(options.gain),
            "--white-balance-temperature", str(options.white_balance_temperature),
            "--auto-focus",
        )
    with tempfile.TemporaryDirectory() as temporary_name:
        temporary = Path(temporary_name)
        quads = detect_panel_quads(raw)
        panels = label_panels(raw, quads, temporary)
        panel_profiles: dict[str, Any] = {}
        for board in BOARDS:
            crop = options.output.parent / f"{board}-registration-crop.png"
            perspective_crop(raw, crop, panels[board])
            measured = quality(crop)
            v1 = temporary / f"{board}.json"
            run(
                "python3", str(options.project / "tools/color_calibration/analyze.py"),
                str(crop), "--json", str(v1), "--corrected",
                str(options.output.parent / f"{board}-registration-corrected.png"),
                "--exposure", str(options.exposure), "--gain", str(options.gain),
                "--white-balance-temperature", str(options.white_balance_temperature),
                "--focus-mode", "auto", "--camera", options.camera,
                "--source-label", f"dual registration {board}",
            )
            calibration = json.loads(v1.read_text())
            panel_profiles[board] = {
                "board": board,
                "expected_physical_display": [320, 240] if board == "cores3" else [240, 240],
                "quadrilateral": panels[board],
                "homography_points": homography(panels[board]),
                "correction": calibration["correction"],
                "quality": {**calibration["quality"], **measured},
            }
    profile = {
        "schema": SCHEMA,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "fixture": {
            "camera": options.camera,
            "frame_dimensions": list(dimensions(raw)),
            "registration_sha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
            "max_viewport_drift_pixels": options.max_drift,
        },
        "capture": {
            "exposure": options.exposure,
            "gain": options.gain,
            "white_balance_temperature": options.white_balance_temperature,
            "focus_mode": "auto",
        },
        "panels": panel_profiles,
    }
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(profile, indent=2) + "\n")
    print(f"dual capture profile: {options.output}")
    return 0


def load_profile(path: Path) -> dict[str, Any]:
    profile = json.loads(path.read_text())
    if profile.get("schema") != SCHEMA or set(profile.get("panels", {})) != set(BOARDS):
        raise ValueError("unsupported or incomplete dual capture profile")
    for board in BOARDS:
        panel = profile["panels"][board]
        quad = panel.get("quadrilateral")
        matrix = panel.get("correction", {}).get("matrix")
        if not isinstance(quad, list) or len(quad) != 4:
            raise ValueError(f"{board} quadrilateral must contain four points")
        if not isinstance(matrix, list) or len(matrix) != 3:
            raise ValueError(f"{board} color correction must be a 3x4 matrix")
    return profile


def wait_for_visual(serials: dict[str, str], scene: str, timeout: float) -> dict[str, Any]:
    descriptors: dict[int, tuple[str, bytearray]] = {}
    original: dict[int, list[Any]] = {}
    try:
        for board, port in serials.items():
            descriptor = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            settings = termios.tcgetattr(descriptor)
            original[descriptor] = settings
            settings[4] = termios.B115200
            settings[5] = termios.B115200
            settings[3] = 0
            termios.tcsetattr(descriptor, termios.TCSANOW, settings)
            descriptors[descriptor] = (board, bytearray())
        ready: dict[str, Any] = {}
        deadline = time.monotonic() + timeout
        while len(ready) != len(serials) and time.monotonic() < deadline:
            readable, _, _ = select.select(list(descriptors), [], [], 0.25)
            for descriptor in readable:
                board, buffer = descriptors[descriptor]
                try:
                    buffer.extend(os.read(descriptor, 4096))
                except BlockingIOError:
                    continue
                for match in VISUAL_RE.finditer(buffer):
                    observed_scene = match.group(2).decode()
                    if observed_scene == scene:
                        ready[board] = {
                            "device_id": match.group(1).decode(),
                            "scene": observed_scene,
                            "revision": int(match.group(3)),
                            "frame_hash": match.group(4).decode().lower(),
                        }
                if len(buffer) > 65536:
                    del buffer[:-32768]
        missing = sorted(set(serials) - set(ready))
        if missing:
            raise TimeoutError(f"visual readiness timed out for: {', '.join(missing)}")
        return ready
    finally:
        for descriptor in descriptors:
            termios.tcsetattr(descriptor, termios.TCSANOW, original[descriptor])
            os.close(descriptor)


def corrected_profile(profile: dict[str, Any], board: str, output: Path) -> None:
    payload = {
        "schema": "doodad.color-calibration.v1",
        "capture": profile["capture"],
        "correction": profile["panels"][board]["correction"],
    }
    output.write_text(json.dumps(payload))


def quad_drift(left: list[list[float]], right: list[list[float]]) -> float:
    return max(math.dist(a, b) for a, b in zip(left, right, strict=True))


def capture(options: argparse.Namespace) -> int:
    profile = load_profile(options.profile.resolve())
    raw = options.raw.resolve()
    readiness = {}
    if not options.offline:
        readiness = wait_for_visual(
            {"cores3": options.cores3_port, "t-watch-s3": options.watch_port},
            options.scene, options.timeout,
        )
    if not raw.exists():
        if options.cleancam is None:
            raise ValueError("raw frame is missing and --cleancam was not supplied")
        raw.parent.mkdir(parents=True, exist_ok=True)
        run(
            str(options.cleancam), "--capture", str(raw),
            "--exposure", str(profile["capture"]["exposure"]),
            "--gain", str(profile["capture"]["gain"]),
            "--white-balance-temperature", str(profile["capture"]["white_balance_temperature"]),
            "--auto-focus",
        )
    if list(dimensions(raw)) != profile["fixture"]["frame_dimensions"]:
        raise ValueError("camera frame dimensions changed; recalibration required")
    detected = label_panels(raw, detect_panel_quads(raw), options.output / "detected")
    max_drift = profile["fixture"]["max_viewport_drift_pixels"]
    for board in BOARDS:
        drift = quad_drift(detected[board], profile["panels"][board]["quadrilateral"])
        if drift > max_drift:
            raise ValueError(f"{board} viewport moved {drift:.1f}px; recalibration required")
    scene_dir = options.output / options.scene
    for name in ("raw", "crop", "corrected", "difference", "comparison"):
        (scene_dir / name).mkdir(parents=True, exist_ok=True)
    full_frame = scene_dir / "raw" / "both-devices.png"
    shutil.copy2(raw, full_frame)
    metrics: dict[str, Any] = {}
    with tempfile.TemporaryDirectory() as temporary_name:
        temporary = Path(temporary_name)
        for board in BOARDS:
            crop = scene_dir / "crop" / f"{board}.png"
            corrected = scene_dir / "corrected" / f"{board}.png"
            perspective_crop(raw, crop, detected[board])
            measured = quality(crop)
            v1 = temporary / f"{board}.json"
            corrected_profile(profile, board, v1)
            run(
                "python3", str(options.project / "tools/color_calibration/apply.py"),
                str(crop), str(corrected), "--profile", str(v1),
                "--exposure", str(profile["capture"]["exposure"]),
                "--gain", str(profile["capture"]["gain"]),
                "--white-balance-temperature", str(profile["capture"]["white_balance_temperature"]),
                "--focus-mode", profile["capture"]["focus_mode"],
            )
            difference = scene_dir / "difference" / f"{board}.png"
            run("magick", str(options.reference), str(corrected), "-compose", "difference", "-composite", str(difference))
            rmse_text = subprocess.run(
                ["magick", "compare", "-metric", "RMSE", str(options.reference), str(corrected), "null:"],
                capture_output=True, text=True,
            ).stderr
            normalized = float(re.search(r"\(([^)]+)\)", rmse_text).group(1))
            metrics[board] = {**measured, "normalized_rmse": normalized, "pass": normalized <= options.max_rmse}
            run(
                "magick", str(options.reference), str(crop), str(corrected), str(difference),
                "+append", str(scene_dir / "comparison" / f"{board}.png"),
            )
    run(
        "magick", str(full_frame),
        str(scene_dir / "comparison" / "cores3.png"),
        str(scene_dir / "comparison" / "t-watch-s3.png"),
        "-append", str(scene_dir / "contact-sheet.png"),
    )
    manifest = {
        "schema": "doodad.visual-scene.v1",
        "scene": options.scene,
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "expected": {"screen": options.expected_screen, "key_text": options.expected_text},
        "profile": str(options.profile.resolve()),
        "camera": profile["capture"],
        "firmware": {board: hashlib.sha256((options.project / f"firmware/build/{board}/doodad_runtime.bin").read_bytes()).hexdigest() for board in BOARDS},
        "readiness": readiness,
        "metrics": metrics,
        "review": {"status": "pending", "reviewer": None, "boards": {}, "notes": "Raw frame and both corrected crops must be opened and inspected."},
    }
    manifest_path = scene_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    if not all(item["pass"] for item in metrics.values()):
        raise ValueError(f"automated image gate failed; see {manifest_path}")
    print(f"evidence ready for explicit visual review: {manifest_path}")
    return 0


def review(options: argparse.Namespace) -> int:
    manifest = json.loads(options.manifest.read_text())
    manifest["review"] = {
        "status": "pass" if options.cores3 == "pass" and options.watch == "pass" else "fail",
        "reviewer": options.reviewer,
        "reviewed_at": datetime.now(timezone.utc).isoformat(),
        "boards": {"cores3": options.cores3, "t-watch-s3": options.watch},
        "checks": ["raw frame", "named screen", "key text and values", "selected state", "clipping", "orientation", "corruption", "cross-device leakage"],
        "notes": options.notes,
    }
    options.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"review recorded: {manifest['review']['status']}")
    return 0 if manifest["review"]["status"] == "pass" else 1


def parser() -> argparse.ArgumentParser:
    project = Path(__file__).resolve().parents[1]
    root = argparse.ArgumentParser()
    commands = root.add_subparsers(dest="command", required=True)
    calibration = commands.add_parser("calibrate")
    calibration.add_argument("--raw", type=Path, required=True)
    calibration.add_argument("--output", type=Path, required=True)
    calibration.add_argument("--cleancam", type=Path)
    calibration.add_argument("--cores3-port", default="/dev/cu.usbmodem21101")
    calibration.add_argument("--watch-port", default="/dev/cu.usbmodem22301")
    calibration.add_argument("--timeout", type=float, default=20.0)
    calibration.add_argument("--offline", action="store_true", help="fixture tests only")
    calibration.add_argument("--camera", default="Logitech StreamCam")
    calibration.add_argument("--exposure", type=int, default=16)
    calibration.add_argument("--gain", type=int, default=58)
    calibration.add_argument("--white-balance-temperature", type=int, default=4000)
    calibration.add_argument("--max-drift", type=float, default=12.0)
    calibration.set_defaults(handler=calibrate, project=project)
    evidence = commands.add_parser("capture")
    evidence.add_argument("--profile", type=Path, required=True)
    evidence.add_argument("--raw", type=Path, required=True)
    evidence.add_argument("--cleancam", type=Path)
    evidence.add_argument("--reference", type=Path, required=True)
    evidence.add_argument("--output", type=Path, required=True)
    evidence.add_argument("--scene", required=True)
    evidence.add_argument("--expected-screen", required=True)
    evidence.add_argument("--expected-text", action="append", default=[])
    evidence.add_argument("--cores3-port", default="/dev/cu.usbmodem21101")
    evidence.add_argument("--watch-port", default="/dev/cu.usbmodem22301")
    evidence.add_argument("--timeout", type=float, default=20.0)
    evidence.add_argument("--max-rmse", type=float, default=0.38)
    evidence.add_argument("--offline", action="store_true", help="fixture tests only; never valid for physical acceptance")
    evidence.set_defaults(handler=capture, project=project)
    record = commands.add_parser("review")
    record.add_argument("--manifest", type=Path, required=True)
    record.add_argument("--reviewer", required=True)
    record.add_argument("--cores3", choices=("pass", "fail"), required=True)
    record.add_argument("--watch", choices=("pass", "fail"), required=True)
    record.add_argument("--notes", required=True)
    record.set_defaults(handler=review, project=project)
    validate = commands.add_parser("validate-profile")
    validate.add_argument("profile", type=Path)
    validate.set_defaults(handler=lambda value: (load_profile(value.profile), print("profile valid"))[1] or 0)
    return root


def main() -> int:
    if shutil.which("magick") is None:
        raise SystemExit("ImageMagick is required")
    options = parser().parse_args()
    try:
        return options.handler(options)
    except (OSError, ValueError, TimeoutError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    raise SystemExit(main())
