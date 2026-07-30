from __future__ import annotations

import hashlib
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "foundation": "ac2c3ff80b04a74feb7a1aa1ef64f2fea860eec1bb54f245cede3b18b5ab07c1",
    "stress": "539c72aff114da63b85225a646a8770993cfbc1024047d2f0df67f542c75423e",
    "components": "1127de5af0682bf55e051722878e960712ddc3e3a25128bd947d5831d2dadf4b",
    "calories": "c54e3b9e0379b24d3f3c04661846a0cbe50ede08763c4031e8a70c9d4c8cb1eb",
    "calculator": "e86f9439c39d302744e6c8f7e095c7481457c3c506fa05207a0954ba0c8cd451",
    "workout": "9b741419cc4f72db7b70f3e640ee854a0678188c0a98f2e20740d329ec252f65",
    "inputs": "d42bf589296a20ef7d4f256217cd6dc4ae74f4f92bf0d159549046fdbf443cc5",
    "voice": "92ad177dc740e384fd9089ac9b05de675f852c6fe9b9b48c71ba494427ca2bb9",
    "navigation": "cdbdbd38d6b4d73d375fd37dfc991767e19579d2018c2eb518e5f50837ea6a0f",
    "system": "6388e86082ce390d9329075eb6823df50293f545aea528011c10b39687bc95c2",
    "transforming-list": "a6efdbdcfb6e1780696893056b2c09b3a7fa53fcb28f1df63ffde3dde03cd079",
    "expressive-depth": "27fa69a448ef82a152f88430a8d42ce47a5be82ec7ccb2e30d21be00e206f618",
    "mockup-hydration": "ab1e5ff7ee852ae00568ed2f592ed90333654fecb2a914520e9a7800064e7d5b",
    "mockup-focus": "0f0f8491593baf17f735ef61814e00714907e0e07011de019e4385870a657119",
    "mockup-travel": "2b9afd3fec28f9fafbd7dc4887810866e1a2a6bf3ebd1479a18d324ac0d1816e",
    "mockup-music": "c93dc7afb7aa5debc3f67644d227820ad5975d653bf24a53448da948a9f2a7ae",
}


class CatalogGoldenTests(unittest.TestCase):
    def test_rgb565_catalog_goldens_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for story, expected_hash in EXPECTED.items():
                output = Path(temporary) / f"{story}.bmp"
                subprocess.run(
                    [
                        str(ROOT / "doodad"),
                        "catalog",
                        "--story",
                        story,
                        "--output",
                        str(output),
                    ],
                    cwd=ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                data = output.read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), expected_hash)
                self.assertEqual(data[:2], b"BM")
                self.assertEqual(struct.unpack_from("<ii", data, 18), (240, 240))
                self.assertEqual(struct.unpack_from("<H", data, 28)[0], 24)


if __name__ == "__main__":
    unittest.main()
