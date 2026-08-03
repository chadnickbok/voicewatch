from __future__ import annotations

import binascii
import copy
import tempfile
import unittest
import zlib
from pathlib import Path
from struct import unpack

from tools.doodad_cli.contract import DoodadError
from tools.doodad_cli.parallax_image import (
    PNG_SIGNATURE,
    RGB888Image,
    contact_sheet_rgb888,
    difference_rgb888,
    draw_node_boundaries_rgb888,
    encode_png_rgb565le,
    encode_png_rgb888,
    overlay_rgb888,
    render_derivatives,
    render_pair_contact_sheet,
    side_by_side_rgb888,
    write_contact_sheet_png,
    write_node_boundary_overlay_png,
    write_render_pair_images,
    write_png_rgb888,
)


def decode_test_png(payload: bytes) -> tuple[int, int, bytes]:
    if not payload.startswith(PNG_SIGNATURE):
        raise AssertionError("missing PNG signature")
    offset = len(PNG_SIGNATURE)
    chunks: list[tuple[bytes, bytes]] = []
    while offset < len(payload):
        length = unpack(">I", payload[offset : offset + 4])[0]
        kind = payload[offset + 4 : offset + 8]
        data_start = offset + 8
        data_end = data_start + length
        data = payload[data_start:data_end]
        checksum = unpack(">I", payload[data_end : data_end + 4])[0]
        calculated = binascii.crc32(kind)
        calculated = binascii.crc32(data, calculated) & 0xFFFFFFFF
        if checksum != calculated:
            raise AssertionError(f"invalid {kind!r} checksum")
        chunks.append((kind, data))
        offset = data_end + 4
        if kind == b"IEND":
            break
    if offset != len(payload):
        raise AssertionError("trailing PNG bytes")
    header = next(data for kind, data in chunks if kind == b"IHDR")
    width, height, depth, color, compression, filtering, interlace = unpack(
        ">IIBBBBB", header
    )
    if (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0):
        raise AssertionError("unexpected PNG encoding")
    compressed = b"".join(data for kind, data in chunks if kind == b"IDAT")
    scanlines = zlib.decompress(compressed)
    row_bytes = width * 3
    output = bytearray()
    for row in range(height):
        offset = row * (row_bytes + 1)
        if scanlines[offset] != 0:
            raise AssertionError("unexpected PNG filter")
        output.extend(scanlines[offset + 1 : offset + row_bytes + 1])
    return width, height, bytes(output)


def boundary_node(
    node_id: str,
    *,
    x: int,
    y: int,
    width: int,
    height: int,
    visible: bool = True,
) -> dict:
    return {
        "id": node_id,
        "parent_id": None,
        "role": "group",
        "label": node_id,
        "value": "",
        "state_description": "",
        "visible": visible,
        "enabled": True,
        "actions": [],
        "bounds_px": {
            "x": x,
            "y": y,
            "width": width,
            "height": height,
        },
        "bounds_dp_q8_8": {
            "x": x * 256,
            "y": y * 256,
            "width": width * 256,
            "height": height * 256,
        },
        "token_roles": {},
    }


def boundary_evidence(
    nodes: list[dict],
    *,
    width: int = 5,
    height: int = 5,
) -> dict:
    return {
        "schema_version": 1,
        "snapshot_sha256": "0" * 64,
        "capture_phase": {
            "id": "resting",
            "state": "resting",
            "animation_fraction_milli": 0,
        },
        "renderer": {
            "kind": "lvgl",
            "mode": "simulator",
            "version": "9.5.0",
            "build_sha256": "0" * 64,
        },
        "profile_id": "test_square",
        "physical_width_px": width,
        "physical_height_px": height,
        "nodes": nodes,
    }


class ParallaxPngTests(unittest.TestCase):
    def test_rgb888_png_round_trips_2x2_pixels(self) -> None:
        pixels = bytes(
            (
                255,
                0,
                0,
                0,
                255,
                0,
                0,
                0,
                255,
                255,
                255,
                255,
            )
        )
        encoded = encode_png_rgb888(pixels, width=2, height=2)
        self.assertEqual(decode_test_png(encoded), (2, 2, pixels))
        self.assertEqual(encoded, encode_png_rgb888(pixels, width=2, height=2))

    def test_rgb565_png_uses_canonical_expansion(self) -> None:
        encoded = encode_png_rgb565le(
            bytes.fromhex("00f8 e007 1f00 ffff"),
            width=2,
            height=2,
        )
        self.assertEqual(
            decode_test_png(encoded),
            (
                2,
                2,
                bytes(
                    (
                        255,
                        0,
                        0,
                        0,
                        255,
                        0,
                        0,
                        0,
                        255,
                        255,
                        255,
                        255,
                    )
                ),
            ),
        )

    def test_large_stored_deflate_stream_round_trips(self) -> None:
        width = 180
        height = 180
        pixels = bytes(
            (index * 37) % 256 for index in range(width * height * 3)
        )
        self.assertEqual(
            decode_test_png(
                encode_png_rgb888(pixels, width=width, height=height)
            ),
            (width, height, pixels),
        )

    def test_two_writes_are_byte_identical(self) -> None:
        pixels = bytes(range(48))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = write_png_rgb888(
                root / "first.png",
                pixels,
                width=4,
                height=4,
            )
            second = write_png_rgb888(
                root / "nested" / "second.png",
                pixels,
                width=4,
                height=4,
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_standard_render_pair_layout_is_ready_for_report_artifacts(
        self,
    ) -> None:
        reference = bytes((255, 0, 0) * 4)
        candidate = bytes.fromhex("00f8 00f8 00f8 00f8")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = write_render_pair_images(
                root / "timer.primary",
                reference,
                candidate,
                width=2,
                height=2,
            )
            self.assertEqual(
                {
                    name: path.name
                    for name, path in paths.as_mapping().items()
                },
                {
                    "reference": "reference.png",
                    "candidate": "candidate.png",
                    "side_by_side": "side_by_side.png",
                    "overlay": "overlay.png",
                    "difference": "difference.png",
                },
            )
            for path in paths.as_mapping().values():
                self.assertTrue(path.is_file())
            self.assertEqual(
                decode_test_png(paths.side_by_side.read_bytes())[:2],
                (4, 2),
            )
            self.assertEqual(
                paths.reference.read_bytes(),
                paths.candidate.read_bytes(),
            )


class ParallaxDerivativeTests(unittest.TestCase):
    def test_side_by_side_preserves_rows_without_resizing(self) -> None:
        reference = bytes(
            (
                1,
                2,
                3,
                4,
                5,
                6,
                7,
                8,
                9,
                10,
                11,
                12,
            )
        )
        candidate = bytes(
            (
                21,
                22,
                23,
                24,
                25,
                26,
                27,
                28,
                29,
                30,
                31,
                32,
            )
        )
        image = side_by_side_rgb888(
            reference,
            candidate,
            width=2,
            height=2,
        )
        self.assertEqual((image.width, image.height), (4, 2))
        self.assertEqual(
            image.pixels,
            reference[:6]
            + candidate[:6]
            + reference[6:]
            + candidate[6:],
        )

    def test_overlay_and_difference_use_exact_integer_channels(self) -> None:
        reference = bytes((0, 10, 255) * 4)
        candidate = bytes((255, 20, 0) * 4)
        overlay = overlay_rgb888(
            reference,
            candidate,
            width=2,
            height=2,
        )
        difference = difference_rgb888(
            reference,
            candidate,
            width=2,
            height=2,
        )
        self.assertEqual(overlay.pixels, bytes((128, 15, 128) * 4))
        self.assertEqual(difference.pixels, bytes((255, 10, 255) * 4))
        self.assertEqual((overlay.width, overlay.height), (2, 2))
        self.assertEqual((difference.width, difference.height), (2, 2))

    def test_standard_derivatives_expand_candidate_rgb565(self) -> None:
        reference = bytes((255, 0, 0) * 4)
        derivatives = render_derivatives(
            reference,
            bytes.fromhex("00f8 00f8 00f8 00f8"),
            width=2,
            height=2,
        )
        self.assertEqual(derivatives.difference.pixels, bytes(12))
        self.assertEqual(derivatives.overlay.pixels, reference)
        self.assertEqual(
            (derivatives.side_by_side.width, derivatives.side_by_side.height),
            (4, 2),
        )

    def test_mismatched_buffer_lengths_are_rejected_not_resized(self) -> None:
        reference = bytes(12)
        candidate = bytes(12)
        with self.assertRaisesRegex(ValueError, "expected 12"):
            side_by_side_rgb888(
                reference[:-1],
                candidate,
                width=2,
                height=2,
            )
        with self.assertRaisesRegex(ValueError, "expected 12"):
            overlay_rgb888(
                reference,
                candidate + b"\x00",
                width=2,
                height=2,
            )
        with self.assertRaisesRegex(ValueError, "expected 8"):
            render_derivatives(
                reference,
                bytes(7),
                width=2,
                height=2,
            )

    def test_alpha_and_dimensions_are_strict_integers(self) -> None:
        pixels = bytes(3)
        with self.assertRaisesRegex(ValueError, "candidate_alpha_milli"):
            overlay_rgb888(
                pixels,
                pixels,
                width=1,
                height=1,
                candidate_alpha_milli=True,
            )
        with self.assertRaisesRegex(ValueError, "positive integer"):
            encode_png_rgb888(b"", width=0, height=1)


class ParallaxContactSheetTests(unittest.TestCase):
    def test_twenty_native_pairs_form_a_five_by_four_sheet(self) -> None:
        cells = [
            RGB888Image(
                2,
                1,
                bytes((index, index + 1, index + 2) * 2),
            )
            for index in range(20)
        ]
        sheet = contact_sheet_rgb888(cells)
        self.assertEqual((sheet.width, sheet.height), (10, 4))
        row_bytes = sheet.width * 3
        for row in range(4):
            expected_row = b"".join(
                cells[row * 5 + column].pixels
                for column in range(5)
            )
            self.assertEqual(
                sheet.pixels[row * row_bytes : (row + 1) * row_bytes],
                expected_row,
            )

    def test_incomplete_final_row_uses_configured_background(self) -> None:
        red = RGB888Image(1, 2, bytes((255, 0, 0) * 2))
        green = RGB888Image(1, 2, bytes((0, 255, 0) * 2))
        blue = RGB888Image(1, 2, bytes((0, 0, 255) * 2))
        sheet = contact_sheet_rgb888(
            (red, green, blue),
            columns=2,
            background_rgb=(7, 8, 9),
        )
        self.assertEqual((sheet.width, sheet.height), (2, 4))
        self.assertEqual(
            sheet.pixels,
            bytes(
                (
                    255, 0, 0, 0, 255, 0,
                    255, 0, 0, 0, 255, 0,
                    0, 0, 255, 7, 8, 9,
                    0, 0, 255, 7, 8, 9,
                )
            ),
        )

    def test_render_pair_contact_sheet_preserves_native_pair_pixels(
        self,
    ) -> None:
        reference_red = bytes((255, 0, 0) * 2)
        reference_blue = bytes((0, 0, 255) * 2)
        red_rgb565le = bytes.fromhex("00f8 00f8")
        green_rgb565le = bytes.fromhex("e007 e007")
        sheet = render_pair_contact_sheet(
            (
                (reference_red, green_rgb565le),
                (reference_blue, red_rgb565le),
            ),
            width=2,
            height=1,
            columns=1,
        )
        self.assertEqual((sheet.width, sheet.height), (4, 2))
        self.assertEqual(
            sheet.pixels,
            reference_red + bytes((0, 255, 0) * 2)
            + reference_blue + reference_red,
        )

    def test_contact_sheet_write_is_byte_deterministic(self) -> None:
        images = (
            RGB888Image(2, 2, bytes((index, 0, 255 - index) * 4))
            for index in range(3)
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = write_contact_sheet_png(
                root / "first.png",
                images,
                columns=2,
            )
            second = write_contact_sheet_png(
                root / "second.png",
                (
                    RGB888Image(
                        2,
                        2,
                        bytes((index, 0, 255 - index) * 4),
                    )
                    for index in range(3)
                ),
                columns=2,
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(decode_test_png(first.read_bytes())[:2], (4, 4))

    def test_contact_sheet_rejects_ambiguous_or_invalid_layouts(self) -> None:
        image = RGB888Image(1, 1, b"\x00\x00\x00")
        with self.assertRaisesRegex(ValueError, "at least one"):
            contact_sheet_rgb888(())
        with self.assertRaisesRegex(ValueError, "positive integer"):
            contact_sheet_rgb888((image,), columns=True)
        with self.assertRaisesRegex(ValueError, "identical dimensions"):
            contact_sheet_rgb888(
                (image, RGB888Image(2, 1, bytes(6))),
            )
        with self.assertRaisesRegex(ValueError, "background_rgb"):
            contact_sheet_rgb888(
                (image,),
                background_rgb=(0, 0, 256),
            )
        with self.assertRaisesRegex(TypeError, "RGB888Image"):
            contact_sheet_rgb888((image, b"\x00\x00\x00"))  # type: ignore[arg-type]


class ParallaxNodeBoundaryTests(unittest.TestCase):
    def test_visible_node_gets_one_pixel_rectangle_without_fill(self) -> None:
        source_pixel = (1, 2, 3)
        source = bytes(source_pixel * 25)
        evidence = boundary_evidence(
            [
                boundary_node(
                    "inner",
                    x=1,
                    y=1,
                    width=3,
                    height=3,
                )
            ]
        )
        overlay = draw_node_boundaries_rgb888(
            source,
            evidence,
            width=5,
            height=5,
            boundary_rgb=(9, 8, 7),
        )
        boundary = {
            (x, y)
            for y in range(1, 4)
            for x in range(1, 4)
            if x in {1, 3} or y in {1, 3}
        }
        for y in range(5):
            for x in range(5):
                offset = (y * 5 + x) * 3
                expected = (9, 8, 7) if (x, y) in boundary else source_pixel
                self.assertEqual(
                    tuple(overlay.pixels[offset : offset + 3]),
                    expected,
                )

    def test_edges_are_clipped_without_moving_offscreen_lines(self) -> None:
        evidence = boundary_evidence(
            [
                boundary_node(
                    "partly_offscreen",
                    x=-1,
                    y=2,
                    width=4,
                    height=3,
                ),
                boundary_node(
                    "fully_offscreen",
                    x=7,
                    y=7,
                    width=2,
                    height=2,
                ),
            ]
        )
        overlay = draw_node_boundaries_rgb888(
            bytes(5 * 5 * 3),
            evidence,
            width=5,
            height=5,
            boundary_rgb=(255, 255, 255),
        )
        expected_boundary = {
            (0, 2),
            (1, 2),
            (2, 2),
            (2, 3),
            (0, 4),
            (1, 4),
            (2, 4),
        }
        actual_boundary = {
            (x, y)
            for y in range(5)
            for x in range(5)
            if overlay.pixels[(y * 5 + x) * 3 : (y * 5 + x + 1) * 3]
            == b"\xff\xff\xff"
        }
        self.assertEqual(actual_boundary, expected_boundary)
        self.assertNotIn((0, 3), actual_boundary)

    def test_hidden_and_zero_area_nodes_are_skipped_without_mutation(
        self,
    ) -> None:
        source = bytearray(range(75))
        evidence = boundary_evidence(
            [
                boundary_node(
                    "hidden",
                    x=0,
                    y=0,
                    width=5,
                    height=5,
                    visible=False,
                ),
                boundary_node(
                    "zero_width",
                    x=2,
                    y=2,
                    width=0,
                    height=3,
                ),
            ]
        )
        source_before = bytes(source)
        evidence_before = copy.deepcopy(evidence)
        overlay = draw_node_boundaries_rgb888(
            source,
            evidence,
            width=5,
            height=5,
        )
        self.assertEqual(overlay.pixels, source_before)
        self.assertEqual(bytes(source), source_before)
        self.assertEqual(evidence, evidence_before)

    def test_boundary_writer_is_byte_deterministic(self) -> None:
        source = bytes((4, 5, 6) * 25)
        evidence = boundary_evidence(
            [boundary_node("full", x=0, y=0, width=5, height=5)]
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = write_node_boundary_overlay_png(
                root / "first.png",
                source,
                evidence,
                width=5,
                height=5,
            )
            second = write_node_boundary_overlay_png(
                root / "second.png",
                source,
                evidence,
                width=5,
                height=5,
            )
            self.assertEqual(first.read_bytes(), second.read_bytes())
            decoded = decode_test_png(first.read_bytes())
            self.assertEqual(decoded[:2], (5, 5))
            self.assertEqual(
                decoded[2],
                draw_node_boundaries_rgb888(
                    source,
                    evidence,
                    width=5,
                    height=5,
                ).pixels,
            )

    def test_boundary_overlay_rejects_invalid_evidence_and_dimensions(
        self,
    ) -> None:
        source = bytes(5 * 5 * 3)
        evidence = boundary_evidence(
            [boundary_node("full", x=0, y=0, width=5, height=5)]
        )
        with self.assertRaisesRegex(ValueError, "do not match"):
            draw_node_boundaries_rgb888(
                source,
                boundary_evidence(
                    [
                        boundary_node(
                            "full",
                            x=0,
                            y=0,
                            width=4,
                            height=5,
                        )
                    ],
                    width=4,
                    height=5,
                ),
                width=5,
                height=5,
            )
        with self.assertRaisesRegex(ValueError, "boundary_rgb"):
            draw_node_boundaries_rgb888(
                source,
                evidence,
                width=5,
                height=5,
                boundary_rgb=(255, 0, 300),
            )
        invalid = copy.deepcopy(evidence)
        invalid["nodes"][0].pop("bounds_px")
        with self.assertRaisesRegex(DoodadError, "bounds_px"):
            draw_node_boundaries_rgb888(
                source,
                invalid,
                width=5,
                height=5,
            )


if __name__ == "__main__":
    unittest.main()
