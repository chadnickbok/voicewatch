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
    "voice": "dc650e2c7a07aa93eb0846de99a9795dcb04f39bfd207f40ecf830cca55098a4",
    "navigation": "cdbdbd38d6b4d73d375fd37dfc991767e19579d2018c2eb518e5f50837ea6a0f",
    "system": "6388e86082ce390d9329075eb6823df50293f545aea528011c10b39687bc95c2",
    "transforming-list": "a6efdbdcfb6e1780696893056b2c09b3a7fa53fcb28f1df63ffde3dde03cd079",
    "expressive-depth": "27fa69a448ef82a152f88430a8d42ce47a5be82ec7ccb2e30d21be00e206f618",
    "mockup-hydration": "ab1e5ff7ee852ae00568ed2f592ed90333654fecb2a914520e9a7800064e7d5b",
    "mockup-focus": "0f0f8491593baf17f735ef61814e00714907e0e07011de019e4385870a657119",
    "mockup-travel": "2b9afd3fec28f9fafbd7dc4887810866e1a2a6bf3ebd1479a18d324ac0d1816e",
    "mockup-music": "c93dc7afb7aa5debc3f67644d227820ad5975d653bf24a53448da948a9f2a7ae",
    "os-home": "204d607441789683cd751194fd1cd004dd23ed779523bef1eecd65e01089151f",
    "os-live-cards": "04566e5812696f6200af2fd5e60d8bc6997733aa8481b8ac20482872eb8a4e5f",
    "os-launcher": "aea7b5550d19b3240c3269fde33965a1258cc5ebdd0be0863a7593d9cda122ab",
    "os-control-center": "56bf7a1d016cc0910e759e303fbe86651fc3de2691d5fa8cb0790e59abad31b6",
    "os-app-manager": "34272ca9a3949b0a5e80ba601cc3a4aa6693b79f7eeb6445987c76f716350a84",
    "os-voice": "42b0fa50a91cde75fbb662446c047922a307c20f1e92de949d34fc3f582266e9",
    "os-app-detail": "e4023a67bc3f4229a5cbdcebe8a5c4e4a60b19f5917170ebe89168ed496ce1bc",
    "os-install-progress": "9ed6e671b55c9a3578011e97568bb2a182633e1ca71dd743e3a5d39e647240b4",
    "os-crash-recovery": "2525fe81d9625ce7699ebe660e5c21913529d281f13bed74cb126473c26c9632",
    "os-notification": "566a24e98d8cf915f8e5ce1caa4ecba974bf18019151c0ed0b2b9c530b93f124",
    "os-permission-review": "34674e4191ed02ee551c2918d5f983b8cb19edeca8691e04ad2c75577d6acaba",
    "os-action-review": "e5afa0a56d6665fe68ef2c814199075ea354eb286aea363e9149576b980ecf3a",
    "os-error": "0a06f9bc9a6804103a16270c7d41031ef616f45fdcae602603da96568ba9bcc1",
    "os-voice-thinking": "a9b45b4179ce385f4257f31bebf50887f88bee0d30f20337b28f3af0fcc12798",
    "os-voice-review": "b98d87493bec19e6bb65299788755449a2869d1901a63fcf33e3fd9703f23b97",
    "os-voice-build": "2eb0563c7af0c4690acdb43af2a72206a4ab0b394362bee1bf850db701818d64",
    "os-voice-result": "9301c1754b69733ee8604f6c3b2beb7c29bd1d986f02a2a357fdfcf97e7ac95f",
    "color-bars": "8a2bff2346fb9e431b7b3fa5e8cff19f703a1385d69f9b6f02ade958da637943",
}


class CatalogGoldenTests(unittest.TestCase):
    def test_rgb565_catalog_goldens_are_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for story, expected_hash in EXPECTED.items():
                with self.subTest(story=story):
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
                    self.assertEqual(
                        hashlib.sha256(data).hexdigest(), expected_hash
                    )
                    self.assertEqual(data[:2], b"BM")
                    self.assertEqual(
                        struct.unpack_from("<ii", data, 18), (240, 240)
                    )
                    self.assertEqual(struct.unpack_from("<H", data, 28)[0], 24)


if __name__ == "__main__":
    unittest.main()
