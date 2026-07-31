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
    "calories": "30a1f14478bb443f23ec058f6dba90ad36c9607678d4bc86ebfe8bf9c4de0985",
    "voice-notes": "199a140cc7035a6ab959ce2cabc3408e75101b7a89b2e7ffe626006c3db6f0a5",
    "medication": "d0f30705bc9bafdfb9e931ba4a36001972dab4d6c96628b24ec39775ff32a255",
    "sensor-recorder": "6f22e0db28a9fbb69a5cebbcc2bc81d9cbb9df9bc4c6fa837e3fa0732a657178",
    "sleep": "5681f0fae9cd5ab5337b5eee0bdbe3ac39c8dbb777eb9b3c0f914779009ea6b3",
    "media": "c7a7689ee072a9c9e8d7c8cf94677423b2801dc5e7400571ea342f164e6e844f",
    "navigation": "52dd0b635704cb77fe23185b54ec34dc56e67b43bed12dccea5257b54fb7962c",
    "transit": "03ba947f79ecec9ab833d541d5c60575f8f186d6d297d69fdd451b1d40076def",
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
