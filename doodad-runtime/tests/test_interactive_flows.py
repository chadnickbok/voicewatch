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
        ("Record note", "voice-notes.recording.summary", "00:18"),
        ("Lose network", "voice-notes.buffered.summary", "Offline · 00:31"),
        ("Reconnect", "voice-notes.transcript.summary", "1 note"),
        ("Save note", "voice-notes.saved.summary", "Saved ✓"),
    ],
    "medication": [
        ("Mark taken", "medication.taken.summary", "Taken · 9:02"),
        ("Add reminder", "medication.editor.summary", "8:00 daily"),
        ("Save", "medication.due.summary", "Vitamin D"),
        ("Snooze 10m", "medication.snoozed.summary", "Due at 9:12"),
    ],
    "sensor-recorder": [
        ("Start recording", "sensor-recorder.recording.summary", "50 Hz · 1,024"),
        ("Pause", "sensor-recorder.paused.summary", "20.5 seconds"),
        ("Export", "sensor-recorder.export.summary", "1,024 samples"),
        ("Export CSV", "sensor-recorder.exported.summary", "session-001.csv"),
    ],
    "sleep": [
        ("Start sleep", "sleep.overnight.summary", "6h 18m"),
        ("Simulate morning", "sleep.summary.summary", "7h 42m"),
        ("View stages", "sleep.stages.summary", "▂▅▃▇▆▂▅"),
        ("History", "sleep.history.summary", "7h 28m avg"),
    ],
    "media": [
        ("Play", "media.playing.summary", "Midnight City"),
        ("Disconnect", "media.offline.summary", "Last at 1:42"),
        ("Reconnect", "media.reconciled.summary", "Playing · 1:45"),
        ("Controls", "media.playing.summary", "Midnight City"),
    ],
    "navigation": [
        ("Start route", "navigation.maneuver.summary", "Right · 200 ft"),
        ("Lose location", "navigation.cached.summary", "Continue 0.3 mi"),
        ("Recover GPS", "navigation.recovered.summary", "Right · 120 ft"),
        ("Next turn", "navigation.maneuver.summary", "Right · 200 ft"),
    ],
    "transit": [
        ("Refresh", "transit.departures.summary", "N · 3 min"),
        ("Go offline", "transit.stale.summary", "N · 2 min"),
        ("Reconnect", "transit.recovered.summary", "N · 4 min"),
        ("Alert", "transit.alert.summary", "N delayed 6 min"),
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
