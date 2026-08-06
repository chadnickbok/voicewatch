#!/usr/bin/env python3
"""Capture and compare the production Agent tracking simulator flow."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from doodad_cli.contract import build_and_stage
from doodad_cli.native import NativeHost
from doodad_cli.parallax_image import (
    RGB888Image,
    write_contact_sheet_png,
    write_png_rgb565le,
)
from doodad_cli.rgb565 import rgb565le_to_rgb888


ROOT = Path(__file__).resolve().parents[1]
MOCKUPS = {
    "01-home": ROOT
    / "evidence/watch-shell/agent-complication-v1/01-home-four-status-items.png",
    "02-agents": ROOT
    / "evidence/watch-shell/agent-complication-v2/02-agents-list.png",
    "03-agent-detail": ROOT
    / "evidence/watch-shell/agent-complication-v2/03-agent-detail.png",
}


def capture(
    host: NativeHost,
    output: Path,
    name: str,
) -> tuple[RGB888Image, bytes, Path]:
    rgb565 = host.framebuffer_rgb565()
    path = output / f"{name}.png"
    write_png_rgb565le(
        path,
        rgb565,
        width=host.WIDTH,
        height=host.HEIGHT,
    )
    return (
        RGB888Image(
            host.WIDTH,
            host.HEIGHT,
            rgb565le_to_rgb888(
                rgb565,
                width=host.WIDTH,
                height=host.HEIGHT,
            ),
        ),
        rgb565,
        path,
    )


def compare_mockup(
    name: str,
    mockup: Path,
    candidate: Path,
    output: Path,
) -> dict[str, object]:
    if not mockup.is_file():
        raise RuntimeError(f"missing selected mockup: {mockup}")
    dimensions = subprocess.run(
        ["magick", "identify", "-format", "%wx%h", str(candidate)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if dimensions != "240x240":
        raise RuntimeError(f"{candidate} is {dimensions}, expected 240x240")

    compared = subprocess.run(
        ["magick", "compare", "-metric", "RMSE", str(mockup), str(candidate), "null:"],
        check=False,
        capture_output=True,
        text=True,
    )
    match = re.search(r"\(([^)]+)\)", compared.stderr)
    if match is None:
        raise RuntimeError(
            f"could not parse ImageMagick RMSE for {name}: {compared.stderr}"
        )
    normalized_rmse = float(match.group(1))

    side_by_side = output / f"{name}-mockup-vs-simulator.png"
    subprocess.run(
        ["magick", str(mockup), str(candidate), "+append", str(side_by_side)],
        check=True,
    )
    difference = output / f"{name}-difference.png"
    subprocess.run(
        ["magick", "compare", str(mockup), str(candidate), str(difference)],
        check=False,
        capture_output=True,
    )
    return {
        "mockup": str(mockup.relative_to(ROOT)),
        "simulator": str(candidate.relative_to(ROOT)),
        "dimensions": dimensions,
        "normalized_rmse": normalized_rmse,
        "side_by_side": str(side_by_side.relative_to(ROOT)),
        "difference": str(difference.relative_to(ROOT)),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "evidence/watch-shell/agent-complication-simulator",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    timer = build_and_stage(ROOT, ROOT / "apps/timer")
    manifest = json.loads(timer.manifest.read_text(encoding="utf-8"))
    frames: list[RGB888Image] = []
    buffers: list[bytes] = []
    captures: dict[str, Path] = {}
    with NativeHost(ROOT) as host:
        host.start_system_shell(
            app_id=manifest["id"],
            app_name=manifest["name"],
            app_detail=f"Version {manifest['version']}  •  ready",
            wasm_path=timer.wasm,
        )
        if host.system_surface() != "watch_face":
            raise RuntimeError("system shell did not start on Home")
        frame, pixels, path = capture(host, output, "01-home")
        frames.append(frame)
        buffers.append(pixels)
        captures["01-home"] = path

        host.click_system_action("system.agents")
        if host.system_surface() != "agents":
            raise RuntimeError("agent complication did not open Agents")
        frame, pixels, path = capture(host, output, "02-agents")
        frames.append(frame)
        buffers.append(pixels)
        captures["02-agents"] = path

        host.click_system_action("agent.building-app")
        if host.system_surface() != "agent_detail":
            raise RuntimeError("agent row did not open Agent detail")
        frame, pixels, path = capture(host, output, "03-agent-detail")
        frames.append(frame)
        buffers.append(pixels)
        captures["03-agent-detail"] = path

        host.click_system_action("system.agent.back")
        if host.system_surface() != "agents":
            raise RuntimeError("detail Back did not restore Agents")
        if host.framebuffer_rgb565() != buffers[1]:
            raise RuntimeError("detail Back did not restore Agents exactly")

        host.system_back()
        if host.system_surface() != "watch_face":
            raise RuntimeError("Agents Back did not restore Home")
        if host.framebuffer_rgb565() != buffers[0]:
            raise RuntimeError("Agents Back did not restore Home exactly")

    if len(set(buffers)) != 3:
        raise RuntimeError("Agent flow states are not visually distinct")

    contact_sheet = write_contact_sheet_png(
        output / "agent-tracking-flow.png",
        frames,
        columns=3,
        background_rgb=(0, 0, 0),
    )
    comparisons = {
        name: compare_mockup(name, MOCKUPS[name], candidate, output)
        for name, candidate in captures.items()
    }
    report = {
        "canvas": "240x240",
        "states": ["home", "agents", "agent_detail"],
        "navigation_restores_exactly": True,
        "contact_sheet": str(contact_sheet.relative_to(ROOT)),
        "comparisons": comparisons,
    }
    report_path = output / "validation.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        "captured Home → Agents → Agent detail and exact Back restoration\n"
        f"contact sheet: {contact_sheet}\n"
        f"validation: {report_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
