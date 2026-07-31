"""Canonical RGB565 little-endian framebuffer conversion and comparison.

All buffers handled by this module are tightly packed in row-major order,
starting at the top-left pixel. RGB565 words are always serialized
little-endian, independent of the host machine's byte order.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


DEFAULT_WIDTH = 240
DEFAULT_HEIGHT = 240


@dataclass(frozen=True)
class RGB565Comparison:
    """Exact RGB565 comparison totals with convenient derived metrics.

    Channel errors are measured after expanding both RGB565 buffers to RGB888
    with bit replication. Keeping the integer totals makes the result fully
    reproducible; ``mae`` and ``rmse`` are derived from those totals.
    """

    width: int
    height: int
    pixel_count: int
    changed_pixels: int
    absolute_error_sum: int
    squared_error_sum: int
    max_channel_error: int

    @property
    def channel_samples(self) -> int:
        return self.pixel_count * 3

    @property
    def changed_pixel_fraction(self) -> float:
        return self.changed_pixels / self.pixel_count

    @property
    def mae(self) -> float:
        """Mean absolute error across all expanded RGB888 channels."""

        return self.absolute_error_sum / self.channel_samples

    @property
    def mse(self) -> float:
        """Mean squared error across all expanded RGB888 channels."""

        return self.squared_error_sum / self.channel_samples

    @property
    def rmse(self) -> float:
        """Root mean squared error across all expanded RGB888 channels."""

        return math.sqrt(self.mse)


def rgb888_to_rgb565le(
    pixels: bytes | bytearray | memoryview,
    *,
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
) -> bytes:
    """Convert tightly packed row-major RGB888 pixels to canonical RGB565LE."""

    pixel_count = _pixel_count(width, height)
    source = _require_length("RGB888 buffer", pixels, pixel_count * 3)
    output = bytearray(pixel_count * 2)
    source_offset = 0
    output_offset = 0
    for _ in range(pixel_count):
        red = source[source_offset]
        green = source[source_offset + 1]
        blue = source[source_offset + 2]
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output[output_offset] = value & 0xFF
        output[output_offset + 1] = value >> 8
        source_offset += 3
        output_offset += 2
    return bytes(output)


def argb8888_to_rgb565le(
    pixels: bytes | bytearray | memoryview,
    *,
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
) -> bytes:
    """Convert byte-ordered ARGB8888 pixels to canonical RGB565LE.

    The alpha byte is intentionally ignored. Callers that require compositing
    must composite onto an opaque background before using this conversion.
    """

    pixel_count = _pixel_count(width, height)
    source = _require_length("ARGB8888 buffer", pixels, pixel_count * 4)
    output = bytearray(pixel_count * 2)
    source_offset = 0
    output_offset = 0
    for _ in range(pixel_count):
        red = source[source_offset + 1]
        green = source[source_offset + 2]
        blue = source[source_offset + 3]
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output[output_offset] = value & 0xFF
        output[output_offset + 1] = value >> 8
        source_offset += 4
        output_offset += 2
    return bytes(output)


def rgb565le_to_rgb888(
    pixels: bytes | bytearray | memoryview,
    *,
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
) -> bytes:
    """Expand canonical RGB565LE into PNG-ready row-major RGB888 bytes.

    Reduced-width channels use bit replication: ``rrrrr`` becomes
    ``rrrrrrrr`` via ``(value << 3) | (value >> 2)``, and the six-bit green
    channel uses ``(value << 2) | (value >> 4)``.
    """

    pixel_count = _pixel_count(width, height)
    source = _require_length("RGB565LE buffer", pixels, pixel_count * 2)
    output = bytearray(pixel_count * 3)
    source_offset = 0
    output_offset = 0
    for _ in range(pixel_count):
        value = source[source_offset] | (source[source_offset + 1] << 8)
        red, green, blue = _expand_rgb565(value)
        output[output_offset] = red
        output[output_offset + 1] = green
        output[output_offset + 2] = blue
        source_offset += 2
        output_offset += 3
    return bytes(output)


def compare_rgb565le(
    expected: bytes | bytearray | memoryview,
    actual: bytes | bytearray | memoryview,
    *,
    width: int = DEFAULT_WIDTH,
    height: int = DEFAULT_HEIGHT,
) -> RGB565Comparison:
    """Compare two canonical RGB565LE frames with deterministic RGB metrics."""

    pixel_count = _pixel_count(width, height)
    byte_count = pixel_count * 2
    expected_bytes = _require_length(
        "expected RGB565LE buffer", expected, byte_count
    )
    actual_bytes = _require_length(
        "actual RGB565LE buffer", actual, byte_count
    )

    changed_pixels = 0
    absolute_error_sum = 0
    squared_error_sum = 0
    max_channel_error = 0

    for offset in range(0, byte_count, 2):
        expected_value = (
            expected_bytes[offset] | (expected_bytes[offset + 1] << 8)
        )
        actual_value = actual_bytes[offset] | (actual_bytes[offset + 1] << 8)
        if expected_value != actual_value:
            changed_pixels += 1

        expected_rgb = _expand_rgb565(expected_value)
        actual_rgb = _expand_rgb565(actual_value)
        for expected_channel, actual_channel in zip(
            expected_rgb, actual_rgb, strict=True
        ):
            error = abs(expected_channel - actual_channel)
            absolute_error_sum += error
            squared_error_sum += error * error
            max_channel_error = max(max_channel_error, error)

    return RGB565Comparison(
        width=width,
        height=height,
        pixel_count=pixel_count,
        changed_pixels=changed_pixels,
        absolute_error_sum=absolute_error_sum,
        squared_error_sum=squared_error_sum,
        max_channel_error=max_channel_error,
    )


def _pixel_count(width: int, height: int) -> int:
    for name, value in (("width", width), ("height", height)):
        if isinstance(value, bool) or not isinstance(value, int):
            raise ValueError(f"{name} must be a positive integer")
        if value <= 0:
            raise ValueError(f"{name} must be a positive integer")
    return width * height


def _require_length(
    name: str,
    pixels: bytes | bytearray | memoryview,
    expected_length: int,
) -> bytes:
    if not isinstance(pixels, (bytes, bytearray, memoryview)):
        raise TypeError(f"{name} must be bytes-like")
    payload = bytes(pixels)
    if len(payload) != expected_length:
        raise ValueError(
            f"{name} has {len(payload)} bytes; expected {expected_length}"
        )
    return payload


def _expand_rgb565(value: int) -> tuple[int, int, int]:
    red_five = (value >> 11) & 0x1F
    green_six = (value >> 5) & 0x3F
    blue_five = value & 0x1F
    return (
        (red_five << 3) | (red_five >> 2),
        (green_six << 2) | (green_six >> 4),
        (blue_five << 3) | (blue_five >> 2),
    )
