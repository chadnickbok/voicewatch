#!/usr/bin/env python3
"""Fetch, normalize, cache, and optionally render a real Weather snapshot."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))

from doodad_cli.contract import build_and_stage
from doodad_cli.native import NativeHost
from doodad_cli.parallax_image import write_png_rgb565le
from weather_provider import OpenMeteoClient, WeatherCache, WeatherProvider


def capture_route(host: NativeHost, output: Path, name: str) -> None:
    write_png_rgb565le(
        output / f"{name}.png",
        host.framebuffer_rgb565(),
        width=host.WIDTH,
        height=host.HEIGHT,
    )
    (output / f"{name}-scene.json").write_text(
        host.scene_snapshot() + "\n", encoding="utf-8"
    )
    (output / f"{name}-semantics.json").write_text(
        host.semantic_snapshot() + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--city", default="San Francisco")
    parser.add_argument("--units", choices=("metric", "imperial"), default="imperial")
    parser.add_argument(
        "--cache", type=Path, default=ROOT / "target" / "weather-provider" / "cache.json"
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "target" / "weather-provider" / "live"
    )
    parser.add_argument(
        "--offline", action="store_true", help="render the persisted snapshot without network access"
    )
    parser.add_argument("--render", action="store_true")
    options = parser.parse_args()

    provider = WeatherProvider(OpenMeteoClient(), WeatherCache(options.cache))
    result = (
        provider.cached(offline=True)
        if options.offline
        else provider.refresh(city=options.city, units=options.units)
    )
    options.output.mkdir(parents=True, exist_ok=True)
    (options.output / "snapshot.json").write_text(
        json.dumps(result.source, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (options.output / "snapshot.cbor").write_bytes(result.cbor)
    metadata = {
        "schema": "doodad.weather-provider-run.v1",
        "freshness": result.freshness,
        "from_cache": result.from_cache,
        "fetched_at": result.fetched_at.isoformat(),
        "error": result.error,
        "payload_bytes": len(result.cbor),
    }
    (options.output / "run.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )

    if options.render:
        package = build_and_stage(ROOT, ROOT / "apps" / "weather")
        with NativeHost(ROOT) as host:
            host.start_wasm(package.wasm)
            host.deliver_weather_payload(result.cbor, freshness=result.freshness)
            if int(result.source["minutes_until_rain"]) >= 0:
                capture_route(host, options.output, "rain")
                metadata["rendered_routes"] = ["rain"]
            else:
                capture_route(host, options.output, "current")
                # Keep the original single-render filename as a convenient
                # stable entry point for scripts and ad-hoc review.
                (options.output / "lvgl.png").write_bytes(
                    (options.output / "current.png").read_bytes()
                )
                routes = [
                    (
                        "hourly",
                        "weather.primary",
                        "weather.hourly",
                    ),
                    (
                        "daily",
                        "weather.daily-action",
                        "weather.daily",
                    ),
                    (
                        "details",
                        "weather.details-action",
                        "weather.details",
                    ),
                ]
                for name, node_id, action_id in routes:
                    host.dispatch_semantic_action(
                        node_id, action_id, "tap"
                    )
                    capture_route(host, options.output, name)
                metadata["rendered_routes"] = [
                    "current", "hourly", "daily", "details"
                ]
            (options.output / "run.json").write_text(
                json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
            )

    print(json.dumps(metadata, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
