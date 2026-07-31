from __future__ import annotations

import math
import unittest

from tools.doodad_cli.rgb565 import (
    DEFAULT_HEIGHT,
    DEFAULT_WIDTH,
    argb8888_to_rgb565le,
    compare_rgb565le,
    rgb565le_to_rgb888,
    rgb888_to_rgb565le,
)


class RGB565ConversionTests(unittest.TestCase):
    def test_known_colors_have_canonical_little_endian_words(self) -> None:
        rgb888 = bytes(
            (
                0,
                0,
                0,
                255,
                255,
                255,
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
                0,
                0,
                255,
                255,
                255,
                0,
                255,
            )
        )
        encoded = rgb888_to_rgb565le(rgb888, width=8, height=1)
        self.assertEqual(
            encoded,
            bytes.fromhex(
                "0000 ffff 00f8 e007 1f00 e0ff ff07 1ff8"
            ),
        )
        self.assertEqual(
            rgb565le_to_rgb888(encoded, width=8, height=1),
            rgb888,
        )

    def test_word_byte_order_is_little_endian(self) -> None:
        self.assertEqual(
            rgb888_to_rgb565le(
                bytes((255, 0, 0, 0, 255, 0, 0, 0, 255)),
                width=3,
                height=1,
            ),
            bytes((0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00)),
        )
        self.assertEqual(
            rgb565le_to_rgb888(
                bytes((0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00)),
                width=3,
                height=1,
            ),
            bytes((255, 0, 0, 0, 255, 0, 0, 0, 255)),
        )

    def test_conversion_preserves_top_to_bottom_row_major_order(self) -> None:
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
        self.assertEqual(
            rgb888_to_rgb565le(pixels, width=2, height=2),
            bytes.fromhex("00f8 e007 1f00 ffff"),
        )

    def test_argb_uses_argb_byte_order_and_ignores_alpha(self) -> None:
        argb = bytes(
            (
                0x00,
                255,
                0,
                0,
                0x7F,
                0,
                255,
                0,
                0xFF,
                0,
                0,
                255,
            )
        )
        self.assertEqual(
            argb8888_to_rgb565le(argb, width=3, height=1),
            bytes.fromhex("00f8 e007 1f00"),
        )

    def test_bit_replication_expands_low_channel_codes(self) -> None:
        one_in_each_channel = ((1 << 11) | (1 << 5) | 1).to_bytes(
            2, "little"
        )
        self.assertEqual(
            rgb565le_to_rgb888(one_in_each_channel, width=1, height=1),
            bytes((8, 4, 8)),
        )

    def test_all_rgb565_words_survive_expand_and_requantize(self) -> None:
        encoded = b"".join(
            value.to_bytes(2, "little") for value in range(1 << 16)
        )
        rgb888 = rgb565le_to_rgb888(encoded, width=256, height=256)
        self.assertEqual(
            rgb888_to_rgb565le(rgb888, width=256, height=256),
            encoded,
        )

    def test_default_dimensions_require_exact_240_square_buffers(self) -> None:
        encoded = bytes(DEFAULT_WIDTH * DEFAULT_HEIGHT * 2)
        decoded = rgb565le_to_rgb888(encoded)
        self.assertEqual(
            len(decoded),
            DEFAULT_WIDTH * DEFAULT_HEIGHT * 3,
        )
        with self.assertRaisesRegex(ValueError, "expected 115200"):
            rgb565le_to_rgb888(encoded[:-1])

    def test_each_source_format_rejects_short_and_long_buffers(self) -> None:
        cases = (
            (rgb888_to_rgb565le, 3, "RGB888 buffer"),
            (argb8888_to_rgb565le, 4, "ARGB8888 buffer"),
            (rgb565le_to_rgb888, 2, "RGB565LE buffer"),
        )
        for converter, byte_count, label in cases:
            with self.subTest(converter=converter.__name__, length="short"):
                with self.assertRaisesRegex(ValueError, label):
                    converter(
                        bytes(byte_count * 2 - 1),
                        width=2,
                        height=1,
                    )
            with self.subTest(converter=converter.__name__, length="long"):
                with self.assertRaisesRegex(ValueError, label):
                    converter(
                        bytes(byte_count * 2 + 1),
                        width=2,
                        height=1,
                    )

    def test_dimensions_must_be_positive_non_boolean_integers(self) -> None:
        invalid_dimensions = (
            (0, 1),
            (-1, 1),
            (1, 0),
            (1, -1),
            (1.0, 1),
            (1, 1.0),
            (True, 1),
            (1, False),
        )
        for width, height in invalid_dimensions:
            with self.subTest(width=width, height=height):
                with self.assertRaisesRegex(
                    ValueError, "must be a positive integer"
                ):
                    rgb565le_to_rgb888(
                        b"",
                        width=width,
                        height=height,
                    )

    def test_converters_accept_bytes_like_objects_and_reject_other_types(
        self,
    ) -> None:
        self.assertEqual(
            rgb565le_to_rgb888(bytearray((0x1F, 0x00)), width=1, height=1),
            bytes((0, 0, 255)),
        )
        self.assertEqual(
            rgb565le_to_rgb888(
                memoryview(bytes((0xE0, 0x07))),
                width=1,
                height=1,
            ),
            bytes((0, 255, 0)),
        )
        with self.assertRaisesRegex(TypeError, "must be bytes-like"):
            rgb565le_to_rgb888([0, 0], width=1, height=1)  # type: ignore[arg-type]


class RGB565ComparisonTests(unittest.TestCase):
    def test_identical_frames_have_exact_zero_metrics(self) -> None:
        frame = bytes.fromhex("0000 ffff 00f8 e007")
        metrics = compare_rgb565le(frame, frame, width=2, height=2)
        self.assertEqual(metrics.width, 2)
        self.assertEqual(metrics.height, 2)
        self.assertEqual(metrics.pixel_count, 4)
        self.assertEqual(metrics.channel_samples, 12)
        self.assertEqual(metrics.changed_pixels, 0)
        self.assertEqual(metrics.changed_pixel_fraction, 0.0)
        self.assertEqual(metrics.absolute_error_sum, 0)
        self.assertEqual(metrics.squared_error_sum, 0)
        self.assertEqual(metrics.max_channel_error, 0)
        self.assertEqual(metrics.mae, 0.0)
        self.assertEqual(metrics.mse, 0.0)
        self.assertEqual(metrics.rmse, 0.0)

    def test_changed_pixel_mae_and_rmse_are_exactly_derived(self) -> None:
        expected = bytes.fromhex("0000 0000")
        one_in_each_channel = (1 << 11) | (1 << 5) | 1
        actual = one_in_each_channel.to_bytes(2, "little") + bytes(2)
        metrics = compare_rgb565le(
            expected,
            actual,
            width=2,
            height=1,
        )
        self.assertEqual(metrics.pixel_count, 2)
        self.assertEqual(metrics.channel_samples, 6)
        self.assertEqual(metrics.changed_pixels, 1)
        self.assertEqual(metrics.changed_pixel_fraction, 0.5)
        self.assertEqual(metrics.absolute_error_sum, 20)
        self.assertEqual(metrics.squared_error_sum, 144)
        self.assertEqual(metrics.max_channel_error, 8)
        self.assertEqual(metrics.mae, 20 / 6)
        self.assertEqual(metrics.mse, 24.0)
        self.assertEqual(metrics.rmse, math.sqrt(24))

    def test_white_against_black_has_full_scale_error(self) -> None:
        metrics = compare_rgb565le(
            bytes.fromhex("0000"),
            bytes.fromhex("ffff"),
            width=1,
            height=1,
        )
        self.assertEqual(metrics.changed_pixels, 1)
        self.assertEqual(metrics.absolute_error_sum, 3 * 255)
        self.assertEqual(metrics.squared_error_sum, 3 * 255 * 255)
        self.assertEqual(metrics.max_channel_error, 255)
        self.assertEqual(metrics.mae, 255.0)
        self.assertEqual(metrics.rmse, 255.0)

    def test_compare_validates_both_buffer_lengths(self) -> None:
        frame = bytes(4)
        with self.assertRaisesRegex(
            ValueError, "expected RGB565LE buffer.*expected 4"
        ):
            compare_rgb565le(frame[:-1], frame, width=2, height=1)
        with self.assertRaisesRegex(
            ValueError, "actual RGB565LE buffer.*expected 4"
        ):
            compare_rgb565le(frame, frame + b"\x00", width=2, height=1)


if __name__ == "__main__":
    unittest.main()
