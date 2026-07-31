from __future__ import annotations

import unittest
from pathlib import Path

from tools.doodad_cli.contract import build_and_stage
from tools.doodad_cli.native import NativeHost


ROOT = Path(__file__).resolve().parents[1]


FLOWS = {
    "calendar": [
        (
            ("calendar.agenda.review", "calendar.open.detail"),
            "calendar.detail.event",
            "2:00 - 2:45",
        ),
        ("I'm going", "calendar.confirmed.event", "You're going"),
        ("Travel view", "calendar.travel.event", "11:00 PDT"),
        ("Reconnect", "calendar.confirmed.event", "You're going"),
    ],
    "voice-notes": [
        (
            ("voice-notes.record", "voice-notes.record"),
            "voice-notes.recording.summary",
            "00:18",
        ),
        ("Finish", "voice-notes.buffered.summary", "00:31"),
        ("Text", "voice-notes.transcript.summary", "1 NOTE"),
        ("Save", "voice-notes.saved.summary", "00:31"),
    ],
    "medication": [
        ("Taken", "medication.taken.summary", "TAKEN"),
        ("Edit", "medication.editor.summary", "8:00"),
        ("Save", "medication.due.summary", "VITAMIN D"),
        ("10 min", "medication.snoozed.summary", "10 MIN"),
    ],
    "sensor-recorder": [
        ("Record", "sensor-recorder.recording.summary", "00:20"),
        ("Pause", "sensor-recorder.paused.summary", "00:20"),
        ("Export", "sensor-recorder.export.summary", "1024"),
        ("Export", "sensor-recorder.exported.summary", "CSV"),
    ],
    "sleep": [
        ("Start", "sleep.overnight.summary", "6:18"),
        ("Morning", "sleep.summary.summary", "7:42"),
        ("Stages", "sleep.stages.summary", "1:36"),
        ("History", "sleep.history.summary", "7:28"),
    ],
    "media": [
        ("Play", "media.playing.summary", "Midnight City"),
        ("Offline", "media.offline.summary", "Last at 1:42"),
        ("Retry", "media.reconciled.summary", "Playing / 1:45"),
        ("Controls", "media.playing.summary", "Midnight City"),
    ],
    "navigation": [
        ("Start", "navigation.maneuver.summary", "200 FT"),
        ("GPS off", "navigation.cached.summary", "0.3 MI"),
        ("Recover", "navigation.recovered.summary", "120 FT"),
        ("Next", "navigation.maneuver.summary", "200 FT"),
    ],
    "transit": [
        ("Times", "transit.departures.summary", "3 MIN"),
        ("Offline", "transit.stale.summary", "2 MIN"),
        ("Retry", "transit.recovered.summary", "4 MIN"),
        ("Alert", "transit.alert.summary", "6 MIN"),
    ],
    "smart-home": [
        ("Toggle light", "smart-home.light.summary", "Light on · 72%"),
        ("Fail next command", "smart-home.rollback.summary", "Light restored"),
        ("Retry", "smart-home.light.summary", "Light on · 72%"),
        ("Front door", "smart-home.confirm.summary", "Unlock front door?"),
        ("Confirm unlock", "smart-home.unlocked.summary", "Unlocked ✓"),
    ],
    "sports": [
        ("Follow game", "sports.live.summary", "SF 3 · LA 2"),
        ("Replay burst", "sports.burst.summary", "SF 5 · LA 2"),
        ("End game", "sports.final.summary", "SF 5 · LA 3"),
        ("Scoring plays", "sports.timeline.summary", "5 runs · 4 plays"),
    ],
    "wallet": [
        ("Show pass", "wallet.pass.summary", "BOARDING 8:10"),
        ("Test bad update", "wallet.rejected.summary", "Signature invalid"),
        ("Review details", "wallet.review.summary", "Issuer mismatch"),
        ("Reject", "wallet.rejected.summary", "Signature invalid"),
        ("Use safe pass", "wallet.pass.summary", "BOARDING 8:10"),
    ],
    "remote-control": [
        ("Trigger", "remote-control.targets.summary", "3 controls"),
        ("Camera shutter", "remote-control.pending.summary", "Sending #73"),
        ("Lose link", "remote-control.offline.summary", "No action sent"),
        ("Reconnect", "remote-control.targets.summary", "3 controls"),
        ("Camera shutter", "remote-control.pending.summary", "Sending #73"),
        ("Deliver ack", "remote-control.done.summary", "Captured ✓"),
    ],
}


class InteractiveMockFlowTests(unittest.TestCase):
    def test_every_flow_crosses_provider_boundary_and_recovers(self) -> None:
        for slug, steps in FLOWS.items():
            with self.subTest(app=slug):
                package = build_and_stage(ROOT, ROOT / "apps" / slug)
                native = NativeHost(ROOT)
                try:
                    native.start_wasm(package.wasm)
                    for action, node_id, expected in steps:
                        if isinstance(action, tuple):
                            native.dispatch_semantic_action(
                                action[0],
                                action[1],
                                "tap",
                            )
                        else:
                            native.click_button(action)
                        self.assertEqual(native.node_text(node_id), expected)
                finally:
                    native.close()


if __name__ == "__main__":
    unittest.main()
