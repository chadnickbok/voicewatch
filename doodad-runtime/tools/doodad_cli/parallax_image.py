"""Deterministic, dependency-free image helpers for Project Parallax.

The comparison pipeline deliberately works with native-size, tightly packed
buffers.  Nothing in this module resizes, color-manages, or otherwise hides a
renderer mismatch.  PNG output uses filter type 0 and stored DEFLATE blocks so
identical pixels produce identical bytes without depending on a compressor's
heuristics.
"""

from __future__ import annotations

import binascii
from dataclasses import dataclass
from pathlib import Path
from struct import pack
from typing import Any, Iterable

from .parallax_contract import validate_node_evidence
from .rgb565 import rgb565le_to_rgb888


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_MAX_DEFLATE_STORED_BLOCK = 65_535


@dataclass(frozen=True)
class RGB888Image:
    """A validated, tightly packed, row-major RGB888 image."""

    width: int
    height: int
    pixels: bytes

    def __post_init__(self) -> None:
        pixel_count = _pixel_count(self.width, self.height)
        object.__setattr__(
            self,
            "pixels",
            _require_pixels("RGB888 image", self.pixels, pixel_count * 3),
        )


@dataclass(frozen=True)
class ImageDerivatives:
    """Native-size visual derivatives for one reference/candidate pair."""

    side_by_side: RGB888Image
    overlay: RGB888Image
    difference: RGB888Image


@dataclass(frozen=True)
class RenderPairImagePaths:
    """Standard image artifact paths written for one render pair."""

    reference: Path
    candidate: Path
    side_by_side: Path
    overlay: Path
    difference: Path

    def as_mapping(self) -> dict[str, Path]:
        return {
            "reference": self.reference,
            "candidate": self.candidate,
            "side_by_side": self.side_by_side,
            "overlay": self.overlay,
            "difference": self.difference,
        }


def contact_sheet_rgb888(
    images: Iterable[RGB888Image],
    *,
    columns: int = 5,
    background_rgb: tuple[int, int, int] = (0, 0, 0),
) -> RGB888Image:
    """Lay out equal-sized images row-major without scaling them.

    The final row is padded with ``background_rgb`` when it has fewer images
    than ``columns``.  With 20 native render-pair images, the default produces
    the Project Parallax 5-by-4 contact sheet.
    """

    if isinstance(columns, bool) or not isinstance(columns, int) or columns <= 0:
        raise ValueError("columns must be a positive integer")
    background = _background_pixel(background_rgb)
    cells = tuple(images)
    if not cells:
        raise ValueError("contact sheet requires at least one image")
    for index, cell in enumerate(cells):
        if not isinstance(cell, RGB888Image):
            raise TypeError(
                f"contact sheet image {index} must be an RGB888Image"
            )

    cell_width = cells[0].width
    cell_height = cells[0].height
    for index, cell in enumerate(cells[1:], start=1):
        if (cell.width, cell.height) != (cell_width, cell_height):
            raise ValueError(
                "contact sheet images must have identical dimensions; "
                f"image {index} is {cell.width}x{cell.height}, expected "
                f"{cell_width}x{cell_height}"
            )

    rows = (len(cells) + columns - 1) // columns
    sheet_width = cell_width * columns
    sheet_height = cell_height * rows
    output = bytearray(background * (sheet_width * sheet_height))
    cell_row_bytes = cell_width * 3
    sheet_row_bytes = sheet_width * 3
    for index, cell in enumerate(cells):
        cell_column = index % columns
        cell_row = index // columns
        target_x_bytes = cell_column * cell_row_bytes
        target_y = cell_row * cell_height
        for source_y in range(cell_height):
            source_offset = source_y * cell_row_bytes
            target_offset = (
                (target_y + source_y) * sheet_row_bytes + target_x_bytes
            )
            output[
                target_offset : target_offset + cell_row_bytes
            ] = cell.pixels[
                source_offset : source_offset + cell_row_bytes
            ]
    return RGB888Image(sheet_width, sheet_height, bytes(output))


def render_pair_contact_sheet(
    pairs: Iterable[
        tuple[
            bytes | bytearray | memoryview,
            bytes | bytearray | memoryview,
        ]
    ],
    *,
    width: int,
    height: int,
    columns: int = 5,
    background_rgb: tuple[int, int, int] = (0, 0, 0),
) -> RGB888Image:
    """Build a contact sheet from native Compose RGB888/LVGL RGB565LE pairs."""

    pair_images = (
        render_derivatives(
            reference_rgb888,
            candidate_rgb565le,
            width=width,
            height=height,
        ).side_by_side
        for reference_rgb888, candidate_rgb565le in pairs
    )
    return contact_sheet_rgb888(
        pair_images,
        columns=columns,
        background_rgb=background_rgb,
    )


def write_contact_sheet_png(
    path: str | Path,
    images: Iterable[RGB888Image],
    *,
    columns: int = 5,
    background_rgb: tuple[int, int, int] = (0, 0, 0),
) -> Path:
    """Write a deterministic PNG contact sheet without resampling cells."""

    sheet = contact_sheet_rgb888(
        images,
        columns=columns,
        background_rgb=background_rgb,
    )
    return write_png_rgb888(
        path,
        sheet.pixels,
        width=sheet.width,
        height=sheet.height,
    )


def draw_node_boundaries_rgb888(
    pixels: bytes | bytearray | memoryview,
    node_evidence: dict[str, Any],
    *,
    width: int,
    height: int,
    boundary_rgb: tuple[int, int, int] = (255, 0, 255),
) -> RGB888Image:
    """Draw clipped one-pixel rectangles for every visible evidence node.

    Bounds use the renderer's physical-pixel coordinates.  Rectangle edges
    retain their original coordinates when clipped, so an off-screen edge is
    omitted rather than moved onto the viewport boundary.  Neither input is
    mutated.
    """

    pixel_count = _pixel_count(width, height)
    source = _require_pixels("RGB888 buffer", pixels, pixel_count * 3)
    validate_node_evidence(node_evidence)
    evidence_dimensions = (
        node_evidence["physical_width_px"],
        node_evidence["physical_height_px"],
    )
    if evidence_dimensions != (width, height):
        raise ValueError(
            "node evidence dimensions "
            f"{evidence_dimensions[0]}x{evidence_dimensions[1]} do not match "
            f"image dimensions {width}x{height}"
        )
    color = _rgb_pixel(boundary_rgb, "boundary_rgb")
    output = bytearray(source)
    for node in node_evidence["nodes"]:
        if not node["visible"]:
            continue
        bounds = node["bounds_px"]
        rectangle_width = bounds["width"]
        rectangle_height = bounds["height"]
        if rectangle_width <= 0 or rectangle_height <= 0:
            continue
        left = bounds["x"]
        top = bounds["y"]
        right = left + rectangle_width - 1
        bottom = top + rectangle_height - 1
        _draw_horizontal_line(
            output,
            image_width=width,
            image_height=height,
            y=top,
            start_x=left,
            end_x=right,
            color=color,
        )
        if bottom != top:
            _draw_horizontal_line(
                output,
                image_width=width,
                image_height=height,
                y=bottom,
                start_x=left,
                end_x=right,
                color=color,
            )
        _draw_vertical_line(
            output,
            image_width=width,
            image_height=height,
            x=left,
            start_y=top,
            end_y=bottom,
            color=color,
        )
        if right != left:
            _draw_vertical_line(
                output,
                image_width=width,
                image_height=height,
                x=right,
                start_y=top,
                end_y=bottom,
                color=color,
            )
    return RGB888Image(width, height, bytes(output))


def write_node_boundary_overlay_png(
    path: str | Path,
    pixels: bytes | bytearray | memoryview,
    node_evidence: dict[str, Any],
    *,
    width: int,
    height: int,
    boundary_rgb: tuple[int, int, int] = (255, 0, 255),
) -> Path:
    """Write a deterministic RGB888 PNG with NodeEvidence boundaries."""

    overlay = draw_node_boundaries_rgb888(
        pixels,
        node_evidence,
        width=width,
        height=height,
        boundary_rgb=boundary_rgb,
    )
    return write_png_rgb888(
        path,
        overlay.pixels,
        width=overlay.width,
        height=overlay.height,
    )


def encode_png_rgb888(
    pixels: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> bytes:
    """Encode tightly packed RGB888 pixels as a deterministic PNG."""

    pixel_count = _pixel_count(width, height)
    source = _require_pixels("RGB888 buffer", pixels, pixel_count * 3)
    row_bytes = width * 3
    scanlines = bytearray((row_bytes + 1) * height)
    source_offset = 0
    target_offset = 0
    for _ in range(height):
        scanlines[target_offset] = 0
        target_offset += 1
        scanlines[target_offset : target_offset + row_bytes] = source[
            source_offset : source_offset + row_bytes
        ]
        source_offset += row_bytes
        target_offset += row_bytes

    header = pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    compressed = _zlib_stored(bytes(scanlines))
    return b"".join(
        (
            PNG_SIGNATURE,
            _png_chunk(b"IHDR", header),
            _png_chunk(b"IDAT", compressed),
            _png_chunk(b"IEND", b""),
        )
    )


def encode_png_rgb565le(
    pixels: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> bytes:
    """Expand canonical RGB565LE pixels and encode them as a PNG."""

    rgb888 = rgb565le_to_rgb888(pixels, width=width, height=height)
    return encode_png_rgb888(rgb888, width=width, height=height)


def write_png_rgb888(
    path: str | Path,
    pixels: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> Path:
    """Write a deterministic RGB888 PNG, creating parent directories."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(
        encode_png_rgb888(pixels, width=width, height=height)
    )
    return destination


def write_png_rgb565le(
    path: str | Path,
    pixels: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> Path:
    """Write a deterministic PNG from canonical RGB565LE pixels."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(
        encode_png_rgb565le(pixels, width=width, height=height)
    )
    return destination


def write_render_pair_images(
    output_directory: str | Path,
    reference_rgb888: bytes | bytearray | memoryview,
    candidate_rgb565le: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> RenderPairImagePaths:
    """Write the five standard, deterministic images for one report case."""

    root = Path(output_directory)
    derivatives = render_derivatives(
        reference_rgb888,
        candidate_rgb565le,
        width=width,
        height=height,
    )
    paths = RenderPairImagePaths(
        reference=root / "reference.png",
        candidate=root / "candidate.png",
        side_by_side=root / "side_by_side.png",
        overlay=root / "overlay.png",
        difference=root / "difference.png",
    )
    write_png_rgb888(
        paths.reference,
        reference_rgb888,
        width=width,
        height=height,
    )
    write_png_rgb565le(
        paths.candidate,
        candidate_rgb565le,
        width=width,
        height=height,
    )
    for destination, image in (
        (paths.side_by_side, derivatives.side_by_side),
        (paths.overlay, derivatives.overlay),
        (paths.difference, derivatives.difference),
    ):
        write_png_rgb888(
            destination,
            image.pixels,
            width=image.width,
            height=image.height,
        )
    return paths


def side_by_side_rgb888(
    reference: bytes | bytearray | memoryview,
    candidate: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> RGB888Image:
    """Place two native-size RGB888 images next to each other without a gap."""

    pixel_count = _pixel_count(width, height)
    byte_count = pixel_count * 3
    left = _require_pixels("reference RGB888 buffer", reference, byte_count)
    right = _require_pixels("candidate RGB888 buffer", candidate, byte_count)
    row_bytes = width * 3
    output = bytearray(byte_count * 2)
    target_offset = 0
    for row in range(height):
        source_offset = row * row_bytes
        output[target_offset : target_offset + row_bytes] = left[
            source_offset : source_offset + row_bytes
        ]
        target_offset += row_bytes
        output[target_offset : target_offset + row_bytes] = right[
            source_offset : source_offset + row_bytes
        ]
        target_offset += row_bytes
    return RGB888Image(width * 2, height, bytes(output))


def overlay_rgb888(
    reference: bytes | bytearray | memoryview,
    candidate: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
    candidate_alpha_milli: int = 500,
) -> RGB888Image:
    """Blend two native-size RGB888 images with integer arithmetic.

    ``candidate_alpha_milli`` is in 0..1000.  Half values round up, so the
    default blend of black and white is 128 rather than depending on floating
    point rounding behavior.
    """

    pixel_count = _pixel_count(width, height)
    byte_count = pixel_count * 3
    left = _require_pixels("reference RGB888 buffer", reference, byte_count)
    right = _require_pixels("candidate RGB888 buffer", candidate, byte_count)
    if (
        isinstance(candidate_alpha_milli, bool)
        or not isinstance(candidate_alpha_milli, int)
        or not 0 <= candidate_alpha_milli <= 1000
    ):
        raise ValueError("candidate_alpha_milli must be an integer in 0..1000")
    reference_alpha = 1000 - candidate_alpha_milli
    output = bytes(
        (
            reference_channel * reference_alpha
            + candidate_channel * candidate_alpha_milli
            + 500
        )
        // 1000
        for reference_channel, candidate_channel in zip(
            left, right, strict=True
        )
    )
    return RGB888Image(width, height, output)


def difference_rgb888(
    reference: bytes | bytearray | memoryview,
    candidate: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> RGB888Image:
    """Return an absolute per-channel difference image at native size."""

    pixel_count = _pixel_count(width, height)
    byte_count = pixel_count * 3
    left = _require_pixels("reference RGB888 buffer", reference, byte_count)
    right = _require_pixels("candidate RGB888 buffer", candidate, byte_count)
    output = bytes(
        abs(reference_channel - candidate_channel)
        for reference_channel, candidate_channel in zip(
            left, right, strict=True
        )
    )
    return RGB888Image(width, height, output)


def render_derivatives(
    reference_rgb888: bytes | bytearray | memoryview,
    candidate_rgb565le: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> ImageDerivatives:
    """Build all standard derivatives for a Compose/LVGL render pair."""

    pixel_count = _pixel_count(width, height)
    reference = _require_pixels(
        "reference RGB888 buffer",
        reference_rgb888,
        pixel_count * 3,
    )
    candidate = rgb565le_to_rgb888(
        candidate_rgb565le,
        width=width,
        height=height,
    )
    return ImageDerivatives(
        side_by_side=side_by_side_rgb888(
            reference,
            candidate,
            width=width,
            height=height,
        ),
        overlay=overlay_rgb888(
            reference,
            candidate,
            width=width,
            height=height,
        ),
        difference=difference_rgb888(
            reference,
            candidate,
            width=width,
            height=height,
        ),
    )


def _pixel_count(width: int, height: int) -> int:
    for name, value in (("width", width), ("height", height)):
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ValueError(f"{name} must be a positive integer")
    return width * height


def _require_pixels(
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


def _background_pixel(background_rgb: tuple[int, int, int]) -> bytes:
    return _rgb_pixel(background_rgb, "background_rgb")


def _rgb_pixel(value: tuple[int, int, int], name: str) -> bytes:
    if (
        not isinstance(value, tuple)
        or len(value) != 3
        or any(
            isinstance(channel, bool)
            or not isinstance(channel, int)
            or not 0 <= channel <= 255
            for channel in value
        )
    ):
        raise ValueError(
            f"{name} must be a tuple of three integers in 0..255"
        )
    return bytes(value)


def _draw_horizontal_line(
    pixels: bytearray,
    *,
    image_width: int,
    image_height: int,
    y: int,
    start_x: int,
    end_x: int,
    color: bytes,
) -> None:
    if y < 0 or y >= image_height:
        return
    clipped_start = max(0, start_x)
    clipped_end = min(image_width - 1, end_x)
    if clipped_start > clipped_end:
        return
    offset = (y * image_width + clipped_start) * 3
    pixels[offset : offset + (clipped_end - clipped_start + 1) * 3] = (
        color * (clipped_end - clipped_start + 1)
    )


def _draw_vertical_line(
    pixels: bytearray,
    *,
    image_width: int,
    image_height: int,
    x: int,
    start_y: int,
    end_y: int,
    color: bytes,
) -> None:
    if x < 0 or x >= image_width:
        return
    clipped_start = max(0, start_y)
    clipped_end = min(image_height - 1, end_y)
    for y in range(clipped_start, clipped_end + 1):
        offset = (y * image_width + x) * 3
        pixels[offset : offset + 3] = color


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return pack(">I", len(payload)) + kind + payload + pack(">I", checksum)


def _zlib_stored(payload: bytes) -> bytes:
    output = bytearray(b"\x78\x01")
    offset = 0
    while offset < len(payload):
        block_length = min(
            _MAX_DEFLATE_STORED_BLOCK,
            len(payload) - offset,
        )
        final = offset + block_length == len(payload)
        output.append(0x01 if final else 0x00)
        output.extend(pack("<H", block_length))
        output.extend(pack("<H", block_length ^ 0xFFFF))
        output.extend(payload[offset : offset + block_length])
        offset += block_length
    output.extend(pack(">I", _adler32(payload)))
    return bytes(output)


def _adler32(payload: bytes) -> int:
    first = 1
    second = 0
    modulus = 65_521
    for offset in range(0, len(payload), 5_552):
        for value in payload[offset : offset + 5_552]:
            first += value
            second += first
        first %= modulus
        second %= modulus
    return (second << 16) | first
