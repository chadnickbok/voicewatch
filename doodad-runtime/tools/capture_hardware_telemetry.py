#!/usr/bin/env python3
"""Capture and summarize bounded CoreS3 display telemetry over USB serial."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import UTC, datetime
from pathlib import Path
from statistics import fmean
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FIRMWARE = ROOT / "firmware" / "build" / "doodad_runtime.bin"
DISPLAY_MARKER = "[display]"
FIELD_PATTERN = re.compile(r"([a-z_]+)=([^\s]+)")
FLOAT_FIELDS = {"fps"}


def parse_display_line(line: str) -> dict[str, int | float | str] | None:
    if DISPLAY_MARKER not in line:
        return None
    fields: dict[str, int | float | str] = {}
    for name, raw_value in FIELD_PATTERN.findall(line.split(DISPLAY_MARKER, 1)[1]):
        if name in FLOAT_FIELDS:
            fields[name] = float(raw_value)
        elif raw_value.isdigit():
            fields[name] = int(raw_value)
        else:
            fields[name] = raw_value
    required = {
        "fps",
        "frames",
        "flushes",
        "pixels",
        "avg_render_us",
        "max_render_us",
        "avg_flush_us",
        "max_flush_us",
        "objects",
        "internal_free",
        "internal_min",
        "internal_largest",
        "psram_free",
        "psram_min",
        "psram_largest",
    }
    return fields if required.issubset(fields) else None


def summarize(samples: list[dict[str, int | float | str]]) -> dict[str, Any]:
    if not samples:
        raise ValueError("no complete display telemetry samples were captured")

    def integer_values(name: str) -> list[int]:
        return [int(sample[name]) for sample in samples]

    def latest_lifetime(name: str, fallback: int | float) -> int | float:
        values = [sample.get(name) for sample in samples if name in sample]
        return max((int(value) for value in values), default=fallback)

    summary = {
        "sample_count": len(samples),
        "fps_mean": round(fmean(float(sample["fps"]) for sample in samples), 3),
        "fps_max": max(float(sample["fps"]) for sample in samples),
        "frames_total": sum(integer_values("frames")),
        "flushes_total": sum(integer_values("flushes")),
        "pixels_total": sum(integer_values("pixels")),
        "avg_render_us_mean": round(fmean(integer_values("avg_render_us")), 1),
        "max_render_us": max(integer_values("max_render_us")),
        "avg_flush_us_mean": round(fmean(integer_values("avg_flush_us")), 1),
        "max_flush_us": max(integer_values("max_flush_us")),
        "touch_presses_total": sum(integer_values("touch_presses")),
        "objects_min": min(integer_values("objects")),
        "objects_max": max(integer_values("objects")),
        "internal_free_min": min(integer_values("internal_free")),
        "internal_minimum_free": min(integer_values("internal_min")),
        "internal_largest_min": min(integer_values("internal_largest")),
        "psram_free_min": min(integer_values("psram_free")),
        "psram_minimum_free": min(integer_values("psram_min")),
        "psram_largest_min": min(integer_values("psram_largest")),
        "transfer": samples[-1].get("transfer", "unknown"),
    }
    summary.update(
        {
            "lifetime_frames": latest_lifetime(
                "total_frames", summary["frames_total"]
            ),
            "lifetime_flushes": latest_lifetime(
                "total_flushes", summary["flushes_total"]
            ),
            "lifetime_pixels": latest_lifetime(
                "total_pixels", summary["pixels_total"]
            ),
            "lifetime_avg_render_us": latest_lifetime(
                "total_avg_render_us", summary["avg_render_us_mean"]
            ),
            "lifetime_max_render_us": latest_lifetime(
                "total_max_render_us", summary["max_render_us"]
            ),
            "lifetime_avg_flush_us": latest_lifetime(
                "total_avg_flush_us", summary["avg_flush_us_mean"]
            ),
            "lifetime_max_flush_us": latest_lifetime(
                "total_max_flush_us", summary["max_flush_us"]
            ),
        }
    )
    return summary


def hard_reset(port: str) -> None:
    """Reset an ESP32-S3 reliably through esptool's USB/JTAG sequence."""
    subprocess.run(
        [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "--port",
            port,
            "run",
        ],
        check=True,
    )


def capture_serial(port: str, duration_seconds: float) -> str:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError(
            "pyserial is required; run with the ESP-IDF Python environment"
        ) from error

    captured = bytearray()
    with serial.Serial(port, 115200, timeout=0.25) as device:
        device.reset_input_buffer()
        deadline = time.monotonic() + duration_seconds
        while time.monotonic() < deadline:
            captured.extend(device.read(4096))
    return captured.decode("utf-8", errors="replace")


def render_markdown(payload: dict[str, Any]) -> str:
    summary = payload["summary"]
    firmware = payload["firmware"]
    return "\n".join(
        [
            "# CoreS3 Weather hardware telemetry",
            "",
            f"Captured: `{payload['captured_at']}`  ",
            f"Port: `{payload['port']}`  ",
            f"Duration: `{payload['duration_seconds']:.1f}s`  ",
            f"Firmware: `{firmware['path']}` ({firmware['bytes']:,} bytes)",
            "",
            "| Metric | Result |",
            "| --- | ---: |",
            f"| Samples | {summary['sample_count']} |",
            f"| Frames / flushes | {summary['frames_total']} / {summary['flushes_total']} |",
            f"| Flushed pixels | {summary['pixels_total']:,} |",
            f"| Lifetime frames / flushes | {summary['lifetime_frames']} / {summary['lifetime_flushes']} |",
            f"| Lifetime flushed pixels | {summary['lifetime_pixels']:,} |",
            f"| Mean / maximum FPS | {summary['fps_mean']:.3f} / {summary['fps_max']:.1f} |",
            f"| Mean / maximum render | {summary['avg_render_us_mean']:,.1f} / {summary['max_render_us']:,} us |",
            f"| Mean / maximum flush | {summary['avg_flush_us_mean']:,.1f} / {summary['max_flush_us']:,} us |",
            f"| Lifetime mean / maximum render | {summary['lifetime_avg_render_us']:,.1f} / {summary['lifetime_max_render_us']:,} us |",
            f"| Lifetime mean / maximum flush | {summary['lifetime_avg_flush_us']:,.1f} / {summary['lifetime_max_flush_us']:,} us |",
            f"| LVGL objects | {summary['objects_min']}–{summary['objects_max']} |",
            f"| Internal free / historical minimum | {summary['internal_free_min']:,} / {summary['internal_minimum_free']:,} B |",
            f"| Internal largest block floor | {summary['internal_largest_min']:,} B |",
            f"| PSRAM free / historical minimum | {summary['psram_free_min']:,} / {summary['psram_minimum_free']:,} B |",
            f"| PSRAM largest block floor | {summary['psram_largest_min']:,} B |",
            f"| Touch presses observed | {summary['touch_presses_total']} |",
            f"| Transfer mode | {summary['transfer']} |",
            "",
            "Idle windows are expected to report zero FPS after the screen has",
            "settled. Render/flush maxima include first paint only when this report",
            "was captured with a hardware reset.",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--duration", type=float, default=12.0)
    parser.add_argument(
        "--reset",
        action="store_true",
        help="hard-reset the attached board before starting the capture window",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    options = parser.parse_args()
    if options.duration < 2:
        parser.error("--duration must be at least 2 seconds")

    options.output.mkdir(parents=True, exist_ok=True)
    if options.reset:
        hard_reset(options.port)
    serial_log = capture_serial(options.port, options.duration)
    (options.output / "serial.log").write_text(serial_log, encoding="utf-8")
    samples = [
        parsed
        for line in serial_log.splitlines()
        if (parsed := parse_display_line(line)) is not None
    ]
    try:
        summary = summarize(samples)
    except ValueError as error:
        (options.output / "capture-error.txt").write_text(
            f"{error}\nSee serial.log for the complete capture.\n",
            encoding="utf-8",
        )
        print(f"error: {error}; raw output preserved in {options.output / 'serial.log'}")
        return 1
    firmware_path = options.firmware.resolve()
    payload = {
        "schema": "doodad.hardware-telemetry.v1",
        "captured_at": datetime.now(UTC).isoformat(),
        "port": options.port,
        "duration_seconds": options.duration,
        "reset_before_capture": options.reset,
        "firmware": {
            "path": str(firmware_path.relative_to(ROOT)),
            "bytes": firmware_path.stat().st_size,
        },
        "samples": samples,
        "summary": summary,
    }
    (options.output / "telemetry.json").write_text(
        json.dumps(payload, indent=2) + "\n",
        encoding="utf-8",
    )
    (options.output / "report.md").write_text(
        render_markdown(payload),
        encoding="utf-8",
    )
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
