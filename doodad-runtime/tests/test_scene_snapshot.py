from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.doodad_cli.appspec_cbor import _encode, compile_canonical_cbor
from tools.doodad_cli.contract import DoodadError
from tools.doodad_cli.native import NativeHost
from tools.doodad_cli.parallax_contract import validate_scene_snapshot


ROOT = Path(__file__).resolve().parents[1]


def component_fixture() -> dict:
    return {
        "schema_version": 1,
        "app_id": "test.parallax",
        "screen": {
            "id": "fixture.screen",
            "type": "screen",
            "props": {
                "gap": "sm",
                "align": "stretch",
                "children": [
                    {
                        "id": "fixture.column",
                        "type": "column",
                        "props": {
                            "gap": "xs",
                            "align": "start",
                            "children": [
                                {
                                    "id": "fixture.text",
                                    "type": "text",
                                    "props": {
                                        "text": "Resolved text",
                                        "style": "title",
                                        "max_lines": 3,
                                        "align": "end",
                                    },
                                    "semantics": {
                                        "value": "semantic value",
                                        "hint": "semantic hint",
                                    },
                                }
                            ],
                        },
                    },
                    {
                        "id": "fixture.row",
                        "type": "row",
                        "props": {
                            "gap": "none",
                            "align": "center",
                            "children": [],
                        },
                    },
                    {
                        "id": "fixture.scroll",
                        "type": "scroll",
                        "props": {
                            "gap": "lg",
                            "align": "stretch",
                            "children": [],
                        },
                    },
                    {
                        "id": "fixture.button",
                        "type": "button",
                        "props": {
                            "label": "Continue",
                            "variant": "outlined",
                            "tone": "tertiary",
                            "size": "large",
                            "icon": "arrow_forward",
                        },
                        "events": {"tap": "activate"},
                        "semantics": {"label": "Continue action"},
                    },
                    {
                        "id": "fixture.card",
                        "type": "card",
                        "props": {
                            "title": "Title",
                            "body": "Body",
                            "tone": "secondary",
                        },
                    },
                    {
                        "id": "fixture.progress",
                        "type": "progress",
                        "props": {
                            "label": "Progress",
                            "value": 3,
                            "maximum": 8,
                            "style": "segmented",
                            "tone": "error",
                        },
                    },
                    {
                        "id": "fixture.stepper",
                        "type": "stepper",
                        "props": {
                            "label": "Count",
                            "value": 4,
                            "unit": "reps",
                            "minimum": 1,
                            "maximum": 10,
                            "step": 1,
                        },
                        "events": {"valueCommitted": "set_count"},
                        "semantics": {"label": "Set count"},
                    },
                    {
                        "id": "fixture.toggle",
                        "type": "toggle",
                        "props": {
                            "label": "Enabled",
                            "checked": True,
                            "tone": "primary",
                        },
                        "events": {"checkedChanged": "set_enabled"},
                        "semantics": {"label": "Enable feature"},
                    },
                    {
                        "id": "fixture.keypad",
                        "type": "keypad",
                        "props": {
                            "keys": ["1", "2", "3", "⌫"],
                            "columns": 2,
                        },
                        "events": {"tap": "key_press"},
                        "semantics": {"label": "Number keypad"},
                    },
                    {
                        "id": "fixture.voice",
                        "type": "voice_orb",
                        "props": {
                            "state": "listening",
                            "transcript": "Book lunch",
                        },
                        "events": {"cancel": "cancel_voice"},
                        "semantics": {
                            "label": "Listening",
                            "hint": "Tap to cancel",
                        },
                    },
                    {
                        "id": "fixture.live",
                        "type": "live_card",
                        "props": {
                            "title": "Run",
                            "body": "12:34",
                            "progress": 0.42,
                            "tone": "neutral",
                        },
                    },
                ],
            },
        },
    }


class SceneSnapshotTests(unittest.TestCase):
    def test_snapshot_preserves_every_component_kind_and_resolved_field(
        self,
    ) -> None:
        payload = compile_canonical_cbor(component_fixture())
        with NativeHost(ROOT) as host:
            host.replay_mount(payload, 1234)
            snapshot = json.loads(host.scene_snapshot())
            validate_scene_snapshot(snapshot)
            evidence = host.node_evidence()
            self.assertEqual(host.wasm_call_count(), 0)
            self.assertEqual(host.scene_revision(), 1)
            self.assertEqual(host.route_generation(), 1)

        by_id = {node["id"]: node for node in snapshot["nodes"]}
        self.assertEqual(
            [node["id"] for node in evidence["nodes"]],
            [node["id"] for node in snapshot["nodes"]],
        )
        self.assertEqual(
            evidence["nodes"][0]["bounds_px"],
            {"x": 0, "y": 0, "width": 240, "height": 240},
        )
        self.assertEqual(
            {node["kind"] for node in snapshot["nodes"]},
            {
                "screen",
                "column",
                "row",
                "scroll",
                "text",
                "button",
                "card",
                "progress",
                "stepper",
                "toggle",
                "keypad",
                "voice_orb",
                "live_card",
            },
        )
        self.assertEqual(by_id["fixture.text"]["props"]["max_lines"], 3)
        self.assertEqual(
            by_id["fixture.text"]["semantics"]["value"],
            "semantic value",
        )
        self.assertEqual(
            by_id["fixture.text"]["semantics"]["hint"],
            "semantic hint",
        )
        self.assertEqual(
            by_id["fixture.button"]["props"]["icon"],
            "arrow_forward",
        )
        self.assertEqual(
            by_id["fixture.voice"]["props"]["state"],
            "listening",
        )
        self.assertEqual(
            by_id["fixture.voice"]["props"]["secondary_text"],
            "Book lunch",
        )
        self.assertEqual(by_id["fixture.live"]["props"]["value"], 42)
        self.assertEqual(by_id["fixture.live"]["props"]["maximum"], 100)

    def test_replay_command_and_rejection_have_atomic_revisions(self) -> None:
        payload = compile_canonical_cbor(component_fixture())
        update = _encode(
            {
                0: 1,
                1: [
                    {
                        0: 0,
                        1: "fixture.text",
                        2: 0,
                        3: "Updated",
                    }
                ],
            }
        )
        with NativeHost(ROOT) as host:
            host.replay_mount(payload, 0)
            before = host.scene_snapshot()
            host.replay_command_batch(update, 5)
            after = json.loads(host.scene_snapshot())
            self.assertNotEqual(before, host.scene_snapshot())
            self.assertEqual(
                {node["id"]: node for node in after["nodes"]}[
                    "fixture.text"
                ]["props"]["primary_text"],
                "Updated",
            )
            self.assertEqual(host.scene_revision(), 2)
            with self.assertRaises(DoodadError):
                host.replay_command_batch(b"\xa0", 10)
            self.assertEqual(host.scene_revision(), 2)
            self.assertEqual(after, json.loads(host.scene_snapshot()))
            self.assertEqual(
                [
                    operation.outcome
                    for operation in host.scene_operations()
                ],
                [0, 0, 1],
            )


if __name__ == "__main__":
    unittest.main()
