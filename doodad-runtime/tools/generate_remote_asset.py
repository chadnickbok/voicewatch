#!/usr/bin/env python3
"""Generate and verify the Remote Control camera-viewfinder fixture."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

from doodad_cli.parallax_image import encode_png_rgb888, write_png_rgb888


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRECTORY = ROOT / "apps" / "remote-control" / "assets"
SOURCE = ASSET_DIRECTORY / "desert-viewfinder-source.png"
PREVIEW = ASSET_DIRECTORY / "desert-viewfinder.png"
MANIFEST = ASSET_DIRECTORY / "remote-control-assets.json"
EMBEDDED_C = (
    ROOT
    / "components"
    / "m3e_lvgl"
    / "src"
    / "assets"
    / "remote_viewfinder_asset.c"
)
SOURCE_WIDTH = 690
SOURCE_HEIGHT = 460
WIDTH = 230
HEIGHT = 150
SCALE = 3
HEADER = struct.Struct("<4sHHBBH")
MAGIC = b"DIMG"
FORMAT_RGB565LE = 1
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_source_png() -> bytes:
    """Decode the committed 8-bit RGB source without optional dependencies."""

    data = SOURCE.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise RuntimeError("Remote viewfinder source is not a PNG")
    offset = len(PNG_SIGNATURE)
    width = height = color_type = bit_depth = interlace = None
    compressed = bytearray()
    while offset < len(data):
        if offset + 12 > len(data):
            raise RuntimeError("Remote viewfinder PNG is truncated")
        length = int.from_bytes(data[offset : offset + 4], "big")
        kind = data[offset + 4 : offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + length
        if payload_end + 4 > len(data):
            raise RuntimeError("Remote viewfinder PNG chunk is truncated")
        payload = data[payload_start:payload_end]
        if kind == b"IHDR":
            (
                width,
                height,
                bit_depth,
                color_type,
                compression,
                filtering,
                interlace,
            ) = struct.unpack(">IIBBBBB", payload)
            if compression != 0 or filtering != 0:
                raise RuntimeError("Unsupported Remote viewfinder PNG encoding")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset = payload_end + 4

    if (
        width != SOURCE_WIDTH
        or height != SOURCE_HEIGHT
        or bit_depth != 8
        or color_type != 2
        or interlace != 0
    ):
        raise RuntimeError(
            "Remote viewfinder source must be 690x460, RGB8, non-interlaced"
        )

    raw = zlib.decompress(bytes(compressed))
    bytes_per_pixel = 3
    stride = SOURCE_WIDTH * bytes_per_pixel
    expected = SOURCE_HEIGHT * (stride + 1)
    if len(raw) != expected:
        raise RuntimeError("Remote viewfinder PNG has an unexpected payload size")

    output = bytearray()
    previous = bytearray(stride)
    source_offset = 0
    for _ in range(SOURCE_HEIGHT):
        filter_type = raw[source_offset]
        source_offset += 1
        encoded = raw[source_offset : source_offset + stride]
        source_offset += stride
        decoded = bytearray(stride)
        for index, value in enumerate(encoded):
            left = decoded[index - bytes_per_pixel] if index >= 3 else 0
            above = previous[index]
            upper_left = previous[index - 3] if index >= 3 else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                predictor = _paeth(left, above, upper_left)
            else:
                raise RuntimeError(
                    f"Unsupported Remote viewfinder PNG filter {filter_type}"
                )
            decoded[index] = (value + predictor) & 0xFF
        output.extend(decoded)
        previous = decoded
    return bytes(output)


def viewfinder_rgb888() -> bytes:
    """Center-crop and average the source to one physical framebuffer region."""

    source = decode_source_png()
    crop_y = (SOURCE_HEIGHT - HEIGHT * SCALE) // 2
    output = bytearray()
    for target_y in range(HEIGHT):
        source_y = crop_y + target_y * SCALE
        for target_x in range(WIDTH):
            source_x = target_x * SCALE
            red = green = blue = 0
            for delta_y in range(SCALE):
                row = (source_y + delta_y) * SOURCE_WIDTH
                for delta_x in range(SCALE):
                    index = (row + source_x + delta_x) * 3
                    red += source[index]
                    green += source[index + 1]
                    blue += source[index + 2]
            output.extend((red // 9, green // 9, blue // 9))
    return bytes(output)


def rgb888_to_rgb565le(pixels: bytes) -> bytes:
    output = bytearray()
    for index in range(0, len(pixels), 3):
        red, green, blue = pixels[index : index + 3]
        packed = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output.extend(struct.pack("<H", packed))
    return bytes(output)


def dimg_payload(pixels: bytes) -> bytes:
    return HEADER.pack(
        MAGIC,
        WIDTH,
        HEIGHT,
        FORMAT_RGB565LE,
        0,
        0,
    ) + rgb888_to_rgb565le(pixels)


def c_source(payload: bytes, sha256: str) -> str:
    rows = []
    for start in range(0, len(payload), 12):
        chunk = payload[start : start + 12]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return (
        "/* Generated by tools/generate_remote_asset.py. */\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        f'const char doodad_remote_viewfinder_sha256[] = "{sha256}";\n'
        "const uint8_t doodad_remote_viewfinder_dimg[] = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "const size_t doodad_remote_viewfinder_dimg_size = "
        "sizeof(doodad_remote_viewfinder_dimg);\n"
    )


def generated_outputs() -> tuple[str, bytes, dict[Path, bytes]]:
    pixels = viewfinder_rgb888()
    payload = dimg_payload(pixels)
    sha256 = hashlib.sha256(payload).hexdigest()
    descriptor = {
        "schema_version": 1,
        "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "assets": [
            {
                "sha256": sha256,
                "path": f"assets/{sha256}.dimg",
                "media_type": "image/vnd.doodad.rgb565le",
                "width": WIDTH,
                "height": HEIGHT,
                "encoded_bytes": len(payload),
                "decoded_bytes": WIDTH * HEIGHT * 2,
            }
        ],
    }
    return (
        sha256,
        pixels,
        {
            ASSET_DIRECTORY / f"{sha256}.dimg": payload,
            MANIFEST: (
                json.dumps(descriptor, indent=2, sort_keys=True) + "\n"
            ).encode(),
            EMBEDDED_C: c_source(payload, sha256).encode(),
        },
    )


def generate() -> str:
    sha256, pixels, outputs = generated_outputs()
    ASSET_DIRECTORY.mkdir(parents=True, exist_ok=True)
    for stale in ASSET_DIRECTORY.glob("*.dimg"):
        if stale not in outputs:
            stale.unlink()
    write_png_rgb888(PREVIEW, pixels, width=WIDTH, height=HEIGHT)
    for path, payload in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    return sha256


def check() -> str:
    sha256, pixels, outputs = generated_outputs()
    failures = []
    for path, expected in outputs.items():
        if not path.is_file():
            failures.append(f"missing {path.relative_to(ROOT)}")
        elif path.read_bytes() != expected:
            failures.append(f"stale {path.relative_to(ROOT)}")
    expected_preview = encode_png_rgb888(
        pixels,
        width=WIDTH,
        height=HEIGHT,
    )
    if not PREVIEW.is_file():
        failures.append(f"missing {PREVIEW.relative_to(ROOT)}")
    elif PREVIEW.read_bytes() != expected_preview:
        failures.append(f"stale {PREVIEW.relative_to(ROOT)}")
    dimgs = sorted(ASSET_DIRECTORY.glob("*.dimg"))
    expected_dimg = ASSET_DIRECTORY / f"{sha256}.dimg"
    if dimgs != [expected_dimg]:
        failures.append("remote asset directory contains stale DIMG payloads")
    if failures:
        raise RuntimeError("\n".join(failures))
    return sha256


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    try:
        sha256 = check() if options.check else generate()
    except (OSError, RuntimeError, ValueError, zlib.error) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Remote viewfinder asset passed: {sha256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
