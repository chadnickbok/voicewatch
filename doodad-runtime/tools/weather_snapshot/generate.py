#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "reference" / "weather-fixtures" / "v2"
SOURCES = FIXTURES / "sources"
GENERATED = FIXTURES / "generated"


def head(major: int, value: int) -> bytes:
    if value < 24:
        return bytes([(major << 5) | value])
    if value <= 0xFF:
        return bytes([(major << 5) | 24, value])
    if value <= 0xFFFF:
        return bytes([(major << 5) | 25]) + struct.pack(">H", value)
    if value <= 0xFFFFFFFF:
        return bytes([(major << 5) | 26]) + struct.pack(">I", value)
    return bytes([(major << 5) | 27]) + struct.pack(">Q", value)


def encode(value: Any) -> bytes:
    if value is None:
        return b"\xf6"
    if isinstance(value, int):
        return head(0, value) if value >= 0 else head(1, -1 - value)
    if isinstance(value, str):
        data = value.encode("utf-8")
        return head(3, len(data)) + data
    if isinstance(value, list):
        return head(4, len(value)) + b"".join(encode(item) for item in value)
    if isinstance(value, dict):
        entries = [(encode(key), encode(item)) for key, item in value.items()]
        entries.sort(key=lambda entry: (len(entry[0]), entry[0]))
        return head(5, len(entries)) + b"".join(
            key + item for key, item in entries
        )
    raise TypeError(f"unsupported CBOR value {type(value).__name__}")


def bounded(value: int, minimum: int, maximum: int, field: str) -> int:
    if not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"{field} must be {minimum}..{maximum}")
    return value


def optional(value: Any, minimum: int, maximum: int, field: str) -> int | None:
    if value is None:
        return None
    return bounded(value, minimum, maximum, field)


def payload(source: dict[str, Any]) -> dict[int, Any]:
    current = source["current"]
    location = source["location"]
    if not isinstance(location, str) or not 1 <= len(location.encode()) <= 48:
        raise ValueError("location must encode to 1..48 bytes")
    current_wire = [
        bounded(current["temperature_tenths"], -2**31, 2**31 - 1, "temperature"),
        optional(current.get("feels_like_tenths"), -2**31, 2**31 - 1, "feels_like"),
        bounded(current["condition"], 0, 15, "condition"),
        optional(current.get("high_tenths"), -2**31, 2**31 - 1, "high"),
        optional(current.get("low_tenths"), -2**31, 2**31 - 1, "low"),
        optional(current.get("precipitation_percent"), 0, 100, "precipitation"),
        optional(current.get("humidity_percent"), 0, 100, "humidity"),
        optional(current.get("wind_speed_tenths"), 0, 65535, "wind_speed"),
        optional(current.get("wind_direction_degrees"), 0, 359, "wind_direction"),
        optional(current.get("uv_index_tenths"), 0, 65535, "uv_index"),
        optional(current.get("sunrise_local_minute"), 0, 1439, "sunrise"),
        optional(current.get("sunset_local_minute"), 0, 1439, "sunset"),
    ]
    hours = source["hours"]
    days = source["days"]
    rain = source["precipitation"]
    if not 1 <= len(hours) <= 7 or not 1 <= len(days) <= 4 or len(rain) != 13:
        raise ValueError("hours, days, and precipitation must be bounded 1..7, 1..4, and 13")
    hour_wire = [
        [
            bounded(item["local_minute"], 0, 1439, "hour.local_minute"),
            bounded(item["temperature_tenths"], -2**31, 2**31 - 1, "hour.temperature"),
            optional(item.get("precipitation_percent"), 0, 100, "hour.precipitation"),
            bounded(item["condition"], 0, 15, "hour.condition"),
        ]
        for item in hours
    ]
    day_wire = [
        [
            bounded(item["weekday"], 0, 6, "day.weekday"),
            bounded(item["low_tenths"], -2**31, 2**31 - 1, "day.low"),
            bounded(item["high_tenths"], -2**31, 2**31 - 1, "day.high"),
            optional(item.get("precipitation_percent"), 0, 100, "day.precipitation"),
            bounded(item["condition"], 0, 15, "day.condition"),
        ]
        for item in days
    ]
    return {
        0: 2,
        1: location,
        2: bounded(source["local_weekday"], 0, 6, "local_weekday"),
        3: bounded(source["local_minute"], 0, 1439, "local_minute"),
        4: current_wire,
        5: hour_wire,
        6: day_wire,
        7: [bounded(value, 0, 100, "precipitation sample") for value in rain],
        8: bounded(source["minutes_until_rain"], -1, 1439, "minutes_until_rain"),
        9: bounded(source["rain_duration_minutes"], 0, 1440, "rain_duration"),
        10: bounded(source["units"], 0, 1, "units"),
        11: bounded(source["data_revision"], 0, 2**64 - 1, "data_revision"),
        12: bounded(source["cache_age_minutes"], 0, 2**64 - 1, "cache_age"),
    }


def outputs() -> dict[Path, bytes]:
    generated: dict[Path, bytes] = {}
    manifest: dict[str, Any] = {"schema": "weather.snapshot.v2", "fixtures": []}
    for path in sorted(SOURCES.glob("*.json")):
        source = json.loads(path.read_text())
        encoded = encode(payload(source))
        if len(encoded) > 512:
            raise ValueError(f"{path.name} is {len(encoded)} bytes; limit is 512")
        destination = GENERATED / f"{path.stem}.cbor"
        generated[destination] = encoded
        manifest["fixtures"].append({
            "name": path.stem,
            "freshness": source["freshness"],
            "bytes": len(encoded),
            "sha256": hashlib.sha256(encoded).hexdigest(),
        })
    generated[GENERATED / "manifest.json"] = (
        json.dumps(manifest, indent=2) + "\n"
    ).encode()
    return generated


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = outputs()
    stale = [path for path, data in expected.items() if not path.exists() or path.read_bytes() != data]
    if args.check:
        if stale:
            raise SystemExit("weather fixtures are stale: " + ", ".join(str(path) for path in stale))
        return
    GENERATED.mkdir(parents=True, exist_ok=True)
    for path, data in expected.items():
        path.write_bytes(data)


if __name__ == "__main__":
    main()
