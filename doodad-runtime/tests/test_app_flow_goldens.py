from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "timer": "149ff7066b4d6bd65551b84eedae4c6f97740b78d0504829691a888c266ae2ca",
    "weather": "11b15dc809450685a457aee796feedd1c9266d2ac964786359fb6a6f8d2d8bef",
    "notifications": "661c5f0388eb18dc78b8c997efb5060e533a72945f159d63cc0eccfa1a0a1729",
    "tasks": "e76ed6a113c6ff8a239c2061ba90a109d6112ffc0de5c29bb5ebc69e29b8d93f",
    "calculator": "cd5331f7fbacdc52726cc9b7b8ae236a7a6ba85049188666374a2e62aad12a17",
    "calendar": "49d8d61d19c233908266a8fa23dee66718c3207cef66d6356286e78055dfe985",
    "workout": "6893d7b33023a56645a693aee4382dd4fb87fdadcb52164bf792aabbd90ce293",
    "calories": "51e5c19fde84271d94160e73b58832aa2a3accffc913a989977e5b9e3aa2419c",
    "voice-notes": "53534c59d380ee2cd76dc4ea1bf9b09de41a910cd2c91a46761893130cb13f5e",
    "medication": "802207cb78d2b0938581461d8d091da4226bbafc03f2f6904d7ded7f2f8635d2",
    "sensor-recorder": "fb0d7aa170aaa0ad24f34d3fa061b235bcc20bade742c9df99afb65858310cff",
    "sleep": "6b2e7abc07517d684418317226f0e33e955ef7dfab67f57c45758a43a6c9f994",
    "media": "5832f924efe1005b45a6c2dcca0f37eb2c8741f0ef06cb12b9105ce0208145b1",
    "navigation": "0032fcfef8c20083769e2a9dad3b314f82f82aa68b6332026154734ba4fba316",
    "transit": "953244212d2649896ada7448108538c7e5060570b31f7b8498d00225548de283",
    "smart-home": "3999f89544cfe232e9756835006e931cbf8e23c0b20b805090e4c78e8f52279a",
    "sports": "e07a47f779ed0561e3762cfa4314a3ec790e2d5b36874f2807a07637e3bac8fd",
    "wallet": "0928aed8daee47cb8e407639933b849d116c8d2cda4bee593ed92600b5d67eae",
    "remote-control": "1e2244029265917ca2a7a05648f15c53ef6b4a59ea416592e6bbac050cf4b9a3",
    "snake": "a979c549a39558e7b873eada8f8d90da676f88a4878b171c94ebfb58ecf1405a",
}

FLOW_DOCUMENT = json.loads(
    (ROOT / "apps" / "conformance-flows.json").read_text()
)
SEQUENCES = FLOW_DOCUMENT["flows"]


class AppFlowGoldenTests(unittest.TestCase):
    def test_decisive_interactive_rgb565_states_are_stable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for slug, expected in EXPECTED.items():
                with self.subTest(app=slug):
                    package = build_and_stage(ROOT, ROOT / "apps" / slug)
                    native = NativeHost(ROOT)
                    try:
                        native.start_wasm(package.wasm)
                        for action in SEQUENCES[slug]:
                            operation = action["kind"]
                            value = action.get("value")
                            if operation == "click":
                                native.click_button(str(value))
                            elif operation == "advance":
                                native.advance_time(int(value))
                            elif operation == "deliver":
                                native.deliver_provider()
                        output = Path(temporary) / f"{slug}.bmp"
                        native.write_bmp(output)
                    finally:
                        native.close()
                    self.assertEqual(
                        hashlib.sha256(output.read_bytes()).hexdigest(),
                        expected,
                    )


if __name__ == "__main__":
    unittest.main()
