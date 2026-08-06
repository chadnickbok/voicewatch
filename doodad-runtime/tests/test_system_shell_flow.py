from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


class SystemShellFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.timer = build_and_stage(ROOT, ROOT / "apps" / "timer")
        cls.manifest = json.loads(
            cls.timer.manifest.read_text(encoding="utf-8")
        )

    def test_home_launcher_app_back_home_and_voice_restore(self) -> None:
        with NativeHost(ROOT) as host:
            host.start_system_shell(
                app_id=self.manifest["id"],
                app_name=self.manifest["name"],
                app_detail=f"Version {self.manifest['version']}  •  ready",
                wasm_path=self.timer.wasm,
            )
            self.assertEqual(host.system_surface(), "watch_face")
            home = host.framebuffer_rgb565()

            host.click_system_action("system.agents")
            self.assertEqual(host.system_surface(), "agents")
            agents = host.framebuffer_rgb565()
            self.assertNotEqual(agents, home)

            host.click_system_action("agent.building-app")
            self.assertEqual(host.system_surface(), "agent_detail")
            detail = host.framebuffer_rgb565()
            self.assertNotIn(detail, (home, agents))

            host.click_system_action("system.agent.back")
            self.assertEqual(host.system_surface(), "agents")
            self.assertEqual(host.framebuffer_rgb565(), agents)

            host.system_back()
            self.assertEqual(host.system_surface(), "watch_face")
            self.assertEqual(host.framebuffer_rgb565(), home)

            host.click_system_action("system.voice")
            self.assertEqual(host.system_surface(), "watch_face")
            self.assertNotEqual(host.framebuffer_rgb565(), home)
            host.system_back()
            self.assertEqual(host.framebuffer_rgb565(), home)

            host.click_system_action("system.apps")
            self.assertEqual(host.system_surface(), "launcher")
            launcher = host.framebuffer_rgb565()
            self.assertNotEqual(launcher, home)

            host.click_system_action(self.manifest["id"])
            self.assertEqual(host.system_surface(), "app")
            app = host.framebuffer_rgb565()
            self.assertNotIn(app, (home, launcher))

            host.system_back()
            self.assertEqual(host.system_surface(), "launcher")
            self.assertEqual(host.framebuffer_rgb565(), launcher)

            host.system_home()
            self.assertEqual(host.system_surface(), "watch_face")
            self.assertEqual(host.framebuffer_rgb565(), home)


if __name__ == "__main__":
    unittest.main()
