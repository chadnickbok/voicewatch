#!/usr/bin/env python3
"""Capture deterministic frames from the production voice overlay animation."""

from __future__ import annotations

import argparse
import json
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


def capture(host: NativeHost, output: Path, name: str) -> tuple[RGB888Image, bytes]:
    rgb565 = host.framebuffer_rgb565()
    write_png_rgb565le(
        output / f"{name}.png",
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
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "evidence" / "watch-shell" / "voice-overlay-animation",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    timer = build_and_stage(ROOT, ROOT / "apps" / "timer")
    manifest = json.loads(timer.manifest.read_text(encoding="utf-8"))
    frames: list[RGB888Image] = []
    buffers: list[bytes] = []
    with NativeHost(ROOT) as host:
        host.start_system_shell(
            app_id=manifest["id"],
            app_name=manifest["name"],
            app_detail=f"Version {manifest['version']}  •  ready",
            wasm_path=timer.wasm,
        )
        host.click_system_action("system.voice")
        if host.system_surface() != "watch_face":
            raise RuntimeError("voice overlay changed the underlying shell surface")

        frame, pixels = capture(host, output, "00-quiet")
        frames.append(frame)
        buffers.append(pixels)
        for index, milliseconds in enumerate((180, 150, 170, 210), start=1):
            host.advance_system_animation(milliseconds)
            frame, pixels = capture(host, output, f"0{index}-pulse")
            frames.append(frame)
            buffers.append(pixels)

    if len(set(buffers)) != len(buffers):
        raise RuntimeError("voice animation did not produce five distinct frames")

    contact_sheet = write_contact_sheet_png(
        output / "voice-overlay-animation.png",
        frames,
        columns=5,
        background_rgb=(0, 0, 0),
    )
    print(f"captured 5 production voice frames\ncontact sheet: {contact_sheet}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
