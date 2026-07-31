from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost
from tools.doodad_cli.scene_trace import run_flow_action


ROOT = Path(__file__).resolve().parents[1]

EXPECTED = {
    "timer": "1b729d24bda3e8a3992d09d3f73c68bf5e5cce25c197f8fdf01fcc4af0920140",
    "weather": "626eef2211634fdc1930925a8b75ebf97fec4600a771321d1ad950ff6ec9595c",
    "notifications": "661c5f0388eb18dc78b8c997efb5060e533a72945f159d63cc0eccfa1a0a1729",
    "tasks": "c11f8816d430395f679697543835bb9b0ec5e3494bdf3e43820c7b7d2918df3a",
    "calculator": "900b2b845c8451e333a0c6ef6f0c005006e8f3f888cdcebbf12c37919d3b73de",
    "calendar": "605afaf378547668bc3849febd6a8c8c88233d340cce60e3cc4347c0b7424882",
    "workout": "1db39fbc0ce7fc3844ca729a30d13eae9d9fe5887d79d82620afac7d9b7070e3",
    "calories": "e11bc82d35b17d28f79e52ad2254e0667bc1d30deba6800f76a0ed00dc696d2a",
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
    "snake": "ae262f324764251a0c07f766e43737ab56baa66c474af4193bae570bfe9ce9c0",
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
                            run_flow_action(native, action)
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
