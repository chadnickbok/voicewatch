from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class CalculatorAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.package = build_and_stage(ROOT, ROOT / "apps" / "calculator")
        cls.native = NativeHost(ROOT)
        cls.native.start_wasm(cls.package.wasm)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.native.close()

    def setUp(self) -> None:
        self.native.click_button("C")

    def test_decimal_arithmetic_and_operator_chaining(self) -> None:
        for key in ("1", "2", ".", "5", "+", "7", ".", "5", "="):
            self.native.click_button(key)
        self.assertEqual(self.native.node_text("calculator.result"), "20")

        for key in ("C", "7", "*", "8", "="):
            self.native.click_button(key)
        self.assertEqual(self.native.node_text("calculator.result"), "56")

    def test_divide_by_zero_recovers_on_next_digit(self) -> None:
        for key in ("9", "/", "0", "="):
            self.native.click_button(key)
        self.assertEqual(self.native.node_text("calculator.result"), "Error")
        self.native.click_button("4")
        self.assertEqual(self.native.node_text("calculator.result"), "4")

    def test_rapid_input_does_not_drop_keys_or_rebuild_result_node(self) -> None:
        before = self.native.node_text("calculator.result")
        self.assertEqual(before, "0")
        for key in "1234567890":
            self.native.click_button(key)
        self.assertEqual(
            self.native.node_text("calculator.result"),
            "1234567890",
        )


if __name__ == "__main__":
    unittest.main()
