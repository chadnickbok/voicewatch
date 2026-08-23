#!/usr/bin/env python3
"""Capture and compare the workout planning flow with its V3 design targets."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.contract import build_and_stage
from doodad_cli.native import NativeHost
from doodad_cli.parallax_compare import (
    audit_node_evidence_quality,
    compare_reference_rgb888_to_candidate_rgb565le,
)
from doodad_cli.parallax_image import (
    render_pair_contact_sheet,
    write_png_rgb888,
    write_render_pair_images,
)


TARGETS = (
    ROOT
    / "reference"
    / "powerlifting-foundations"
    / "generated"
    / "concepts-v3"
    / "targets"
)


def reference_pixels(path: Path, temporary: Path) -> bytes:
    raw = temporary / f"{path.stem}.rgb"
    subprocess.run(
        ["magick", str(path), "-alpha", "off", "-depth", "8", f"rgb:{raw}"],
        check=True,
    )
    pixels = raw.read_bytes()
    expected = NativeHost.WIDTH * NativeHost.HEIGHT * 3
    if len(pixels) != expected:
        raise RuntimeError(f"{path} produced {len(pixels)} RGB bytes, expected {expected}")
    return pixels


def capture_case(
    host: NativeHost,
    output: Path,
    name: str,
    target: Path,
    temporary: Path,
) -> tuple[bytes, bytes, dict[str, object]]:
    reference = reference_pixels(target, temporary)
    candidate = host.framebuffer_rgb565()
    case = output / "cases" / name
    paths = write_render_pair_images(
        case,
        reference,
        candidate,
        width=host.WIDTH,
        height=host.HEIGHT,
    )
    evidence = host.node_evidence()
    quality = audit_node_evidence_quality(evidence)
    metrics = compare_reference_rgb888_to_candidate_rgb565le(
        reference,
        candidate,
        width=host.WIDTH,
        height=host.HEIGHT,
    )
    result: dict[str, object] = {
        "screen_id": json.loads(host.semantic_snapshot())["nodes"][0]["id"],
        "target": target.relative_to(ROOT).as_posix(),
        "pixel": metrics.to_dict(),
        "quality": quality.to_dict(),
        "artifacts": {
            key: value.relative_to(output).as_posix()
            for key, value in paths.as_mapping().items()
        },
    }
    (case / "metrics.json").write_text(
        json.dumps(result, indent=2) + "\n",
        encoding="utf-8",
    )
    (case / "semantic-snapshot.json").write_text(
        json.dumps(json.loads(host.semantic_snapshot()), indent=2) + "\n",
        encoding="utf-8",
    )
    (case / "node-evidence.json").write_text(
        json.dumps(evidence, indent=2) + "\n",
        encoding="utf-8",
    )
    return reference, candidate, result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT
        / "reference"
        / "powerlifting-foundations"
        / "evidence"
        / "planning-v3",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    package = build_and_stage(ROOT, ROOT / "apps" / "workout")
    pairs: list[tuple[bytes, bytes]] = []
    results: list[dict[str, object]] = []

    with tempfile.TemporaryDirectory(prefix="workout-planning-") as temp_name:
        temporary = Path(temp_name)
        with NativeHost(ROOT) as host:
            host.start_wasm(package.wasm)
            host.dispatch_semantic_action(
                "powerlifting.today.volume", "workout.manage", "tap"
            )
            reference, candidate, result = capture_case(
                host,
                output,
                "13-training-hub",
                TARGETS / "13-training-hub.png",
                temporary,
            )
            pairs.append((reference, candidate))
            results.append(result)

            host.dispatch_semantic_action(
                "powerlifting.training-hub.plan", "workout.plan.edit", "tap"
            )
            reference, candidate, result = capture_case(
                host,
                output,
                "14-workout-builder",
                TARGETS / "14-workout-builder.png",
                temporary,
            )
            pairs.append((reference, candidate))
            results.append(result)

            host.dispatch_semantic_action(
                "powerlifting.workout-builder.squat",
                "workout.plan.exercise",
                "tap",
            )
            reference, candidate, result = capture_case(
                host,
                output,
                "15-exercise-prescription",
                TARGETS / "15-exercise-prescription.png",
                temporary,
            )
            pairs.append((reference, candidate))
            results.append(result)

            host.click_button("DONE")
            host.click_button("SAVE PLAN")
            host.dispatch_semantic_action(
                "powerlifting.training-hub.goal", "workout.goal.edit", "tap"
            )
            reference, candidate, result = capture_case(
                host,
                output,
                "16-strength-goal",
                TARGETS / "16-strength-goal.png",
                temporary,
            )
            pairs.append((reference, candidate))
            results.append(result)

    contact = render_pair_contact_sheet(
        pairs,
        width=NativeHost.WIDTH,
        height=NativeHost.HEIGHT,
        columns=2,
    )
    write_png_rgb888(
        output / "contact-sheet.png",
        contact.pixels,
        width=contact.width,
        height=contact.height,
    )
    report = {"schema_version": 1, "cases": results}
    (output / "report.json").write_text(
        json.dumps(report, indent=2) + "\n",
        encoding="utf-8",
    )
    for result in results:
        pixel = result["pixel"]
        assert isinstance(pixel, dict)
        print(
            f"{result['screen_id']}: changed={pixel['changed_pixel_fraction']:.4f} "
            f"mae={pixel['mae']:.4f} quality={len(result['quality']['issues'])}"
        )
    print(f"contact sheet: {(output / 'contact-sheet.png').relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
