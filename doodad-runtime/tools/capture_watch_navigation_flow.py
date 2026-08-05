#!/usr/bin/env python3
"""Capture the first trusted-shell navigation flow with production rendering."""

from __future__ import annotations

import argparse
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
WATCH_FACE_STORY = 16
LAUNCHER_STORY = 18


def capture(
    host: NativeHost,
    output: Path,
    name: str,
) -> tuple[RGB888Image, bytes]:
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
        default=ROOT / "evidence" / "watch-shell" / "initial-navigation",
    )
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    timer = build_and_stage(ROOT, ROOT / "apps" / "timer")
    frames: list[RGB888Image] = []
    buffers: list[bytes] = []
    with NativeHost(ROOT) as host:
        host.show_catalog(WATCH_FACE_STORY)
        frame, pixels = capture(host, output, "01-watch-face")
        frames.append(frame)
        buffers.append(pixels)

        host.show_catalog(LAUNCHER_STORY)
        frame, pixels = capture(host, output, "02-launcher")
        frames.append(frame)
        buffers.append(pixels)

        host.start_wasm(timer.wasm)
        frame, pixels = capture(host, output, "03-timer-app")
        frames.append(frame)
        buffers.append(pixels)

        host.show_catalog(LAUNCHER_STORY)
        frame, pixels = capture(host, output, "04-back-to-launcher")
        frames.append(frame)
        buffers.append(pixels)

        host.show_catalog(WATCH_FACE_STORY)
        frame, pixels = capture(host, output, "05-home")
        frames.append(frame)
        buffers.append(pixels)

    if buffers[0] != buffers[4]:
        raise RuntimeError("Home did not restore the watch face exactly")
    if buffers[1] != buffers[3]:
        raise RuntimeError("Back did not restore the launcher exactly")
    if len({buffer for buffer in buffers[:3]}) != 3:
        raise RuntimeError("flow states are not visually distinct")

    contact_sheet = write_contact_sheet_png(
        output / "watch-face-launcher-app-home.png",
        frames,
        columns=5,
        background_rgb=(5, 8, 16),
    )
    print(f"captured 5 production-rendered states\ncontact sheet: {contact_sheet}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
