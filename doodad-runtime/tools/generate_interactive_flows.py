#!/usr/bin/env python3
"""Generate deterministic multi-screen interaction fixtures.

These packages exercise the real AppSpec/Wasm/host capability path while their
external integrations are still deterministic. Product-specific state
machines (Timer, Weather, Notifications, Tasks, Calculator, Calories, Workout,
and Snake) live outside this generator.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from doodad_cli.appspec import validate_appspec
from doodad_cli.appspec_cbor import compile_canonical_cbor


ROOT = Path(__file__).resolve().parents[1]


def screen(
    heading: str,
    summary: str,
    body: str,
    primary: tuple[str, str, str],
    secondary: tuple[str, str, str] | None = None,
) -> dict[str, Any]:
    return {
        "heading": heading,
        "summary": summary,
        "body": body,
        "primary": primary,
        "secondary": secondary,
    }


FLOWS: dict[str, dict[str, Any]] = {
    "calendar": {
        "initial": ("calendar.primary", "agenda"),
        "screens": {
            "agenda": screen(
                "TODAY",
                "2 events",
                "11:00 Stand-up\n2:00 Design review",
                ("Open event", "calendar.open.detail", "detail"),
                ("Travel mode", "calendar.travel", "travel"),
            ),
            "detail": screen(
                "DESIGN REVIEW",
                "2:00–2:45",
                "Studio · 4 guests · cached",
                ("RSVP yes", "calendar.rsvp.yes", "confirmed"),
                ("Back to agenda", "calendar.back", "agenda"),
            ),
            "confirmed": screen(
                "RSVP RECORDED",
                "Going ✓",
                "Queued offline · revision 4",
                ("Simulate travel", "calendar.travel", "travel"),
                ("Agenda", "calendar.back", "agenda"),
            ),
            "travel": screen(
                "TIME ZONE",
                "11:00 PDT",
                "Same event · DST-safe · offline",
                ("Reconnect", "calendar.reconnect", "confirmed"),
                ("Agenda", "calendar.back", "agenda"),
            ),
        },
    },
    "voice-notes": {
        "initial": ("voice-notes.primary", "recording"),
        "screens": {
            "recording": screen(
                "RECORDING",
                "00:18",
                "▁▃▅▇▆▃▅ · 3 chunks buffered",
                ("Lose network", "voice-notes.disconnect", "buffered"),
                ("Finish", "voice-notes.finish", "transcript"),
            ),
            "buffered": screen(
                "STILL RECORDING",
                "Offline · 00:31",
                "Audio is safe locally · upload paused",
                ("Reconnect", "voice-notes.reconnect", "transcript"),
                ("Pause", "voice-notes.pause", "buffered"),
            ),
            "transcript": screen(
                "TRANSCRIBED",
                "1 note",
                "“Book the train after lunch.”",
                ("Save note", "voice-notes.save", "saved"),
                ("Record again", "voice-notes.again", "recording"),
            ),
            "saved": screen(
                "VOICE NOTES",
                "Saved ✓",
                "Upload resumed · no duplicate chunks",
                ("Open note", "voice-notes.open", "transcript"),
                ("Home", "voice-notes.home", "home"),
            ),
        },
    },
    "medication": {
        "initial": ("medication.primary", "taken"),
        "screens": {
            "taken": screen(
                "VITAMIN D",
                "Taken · 9:02",
                "Recorded once · 12 day streak",
                ("Add reminder", "medication.add", "editor"),
                ("Undo", "medication.undo", "due"),
            ),
            "editor": screen(
                "NEW REMINDER",
                "8:00 daily",
                "Schedule survives app replacement",
                ("Save", "medication.save", "due"),
                ("Cancel", "medication.cancel", "taken"),
            ),
            "due": screen(
                "MEDICATION DUE",
                "Vitamin D",
                "One exact notification · private",
                ("Take now", "medication.take", "taken"),
                ("Snooze 10m", "medication.snooze", "snoozed"),
            ),
            "snoozed": screen(
                "SNOOZED",
                "Due at 9:12",
                "Journaled once · haptic armed",
                ("Take now", "medication.take", "taken"),
                ("Schedule", "medication.schedule", "editor"),
            ),
        },
    },
    "sensor-recorder": {
        "initial": ("sensor-recorder.primary", "recording"),
        "screens": {
            "recording": screen(
                "RECORDING",
                "50 Hz · 1,024",
                "X +0.04  Y −0.12  Z +0.98",
                ("Pause", "sensor.pause", "paused"),
                ("Finish", "sensor.finish", "export"),
            ),
            "paused": screen(
                "PAUSED",
                "20.5 seconds",
                "Samples committed · UI idle",
                ("Resume", "sensor.resume", "recording"),
                ("Export", "sensor.export", "export"),
            ),
            "export": screen(
                "SESSION",
                "1,024 samples",
                "No gaps · 48 KiB · checksum OK",
                ("Export CSV", "sensor.export.csv", "exported"),
                ("Record again", "sensor.again", "recording"),
            ),
            "exported": screen(
                "EXPORT READY",
                "session-001.csv",
                "Mock transfer complete · revision 8",
                ("Session", "sensor.session", "export"),
                ("Home", "sensor.home", "home"),
            ),
        },
    },
    "sleep": {
        "initial": ("sleep.primary", "overnight"),
        "screens": {
            "overnight": screen(
                "SLEEPING",
                "6h 18m",
                "Low-power motion service · UI inactive",
                ("Simulate morning", "sleep.morning", "summary"),
                ("Wake now", "sleep.wake", "summary"),
            ),
            "summary": screen(
                "GOOD MORNING",
                "7h 42m",
                "Deep 1h 36 · Restful 82%",
                ("View stages", "sleep.stages", "stages"),
                ("Start again", "sleep.again", "overnight"),
            ),
            "stages": screen(
                "SLEEP STAGES",
                "▂▅▃▇▆▂▅",
                "Smart alarm fired once at 7:18",
                ("History", "sleep.history", "history"),
                ("Summary", "sleep.summary", "summary"),
            ),
            "history": screen(
                "7 DAY HISTORY",
                "7h 28m avg",
                "Budget 3.2% · no retained UI",
                ("Last night", "sleep.last", "summary"),
                ("Home", "sleep.home", "home"),
            ),
        },
    },
    "media": {
        "initial": ("media.primary", "playing"),
        "screens": {
            "playing": screen(
                "NOW PLAYING",
                "Midnight City",
                "M83 · 1:42 / 4:03 · Living Room",
                ("Pause", "media.pause", "paused"),
                ("Disconnect", "media.disconnect", "offline"),
            ),
            "paused": screen(
                "PAUSED",
                "1:42",
                "Optimistic command acknowledged #41",
                ("Play", "media.play", "playing"),
                ("Disconnect", "media.disconnect", "offline"),
            ),
            "offline": screen(
                "CONNECTION LOST",
                "Last at 1:42",
                "Play request queued once",
                ("Reconnect", "media.reconnect", "reconciled"),
                ("Keep cached", "media.cached", "offline"),
            ),
            "reconciled": screen(
                "RECONCILED",
                "Playing · 1:45",
                "Phone state wins · no double action",
                ("Controls", "media.controls", "playing"),
                ("Home", "media.home", "home"),
            ),
        },
    },
    "navigation": {
        "initial": ("navigation.primary", "maneuver"),
        "screens": {
            "maneuver": screen(
                "NEXT TURN",
                "Right · 200 ft",
                "Market St · haptic in 8 sec",
                ("Lose location", "navigation.lose", "cached"),
                ("Route overview", "navigation.overview", "overview"),
            ),
            "overview": screen(
                "ROUTE",
                "1.4 mi · 8 min",
                "3 maneuvers cached · north-up",
                ("Continue", "navigation.continue", "maneuver"),
                ("Lose location", "navigation.lose", "cached"),
            ),
            "cached": screen(
                "GPS UNAVAILABLE",
                "Continue 0.3 mi",
                "Using cached route + compass",
                ("Recover GPS", "navigation.recover", "recovered"),
                ("Overview", "navigation.overview", "overview"),
            ),
            "recovered": screen(
                "ROUTE RECOVERED",
                "Right · 120 ft",
                "Progress monotonic · no backward jump",
                ("Next turn", "navigation.next", "maneuver"),
                ("Home", "navigation.home", "home"),
            ),
        },
    },
    "transit": {
        "initial": ("transit.primary", "departures"),
        "screens": {
            "departures": screen(
                "CASTRO STATION",
                "N · 3 min",
                "N  3 min\nN  14 min\nL  8 min",
                ("Go offline", "transit.offline", "stale"),
                ("Service alert", "transit.alert", "alert"),
            ),
            "stale": screen(
                "CACHED DEPARTURES",
                "N · 2 min",
                "18 minutes old · refreshing…",
                ("Reconnect", "transit.reconnect", "recovered"),
                ("Keep cache", "transit.cache", "stale"),
            ),
            "alert": screen(
                "SERVICE ALERT",
                "N delayed 6 min",
                "Track work near Duboce",
                ("Departures", "transit.departures", "departures"),
                ("Go offline", "transit.offline", "stale"),
            ),
            "recovered": screen(
                "UPDATED NOW",
                "N · 4 min",
                "Revision 12 · Castro selection kept",
                ("Alert", "transit.alert", "alert"),
                ("Refresh again", "transit.refresh", "departures"),
            ),
        },
    },
    "smart-home": {
        "initial": ("smart-home.primary", "light"),
        "screens": {
            "light": screen(
                "LIVING ROOM",
                "Light on · 72%",
                "Optimistic update · ack #18",
                ("Fail next command", "home.fail", "rollback"),
                ("Front door", "home.lock", "confirm"),
            ),
            "rollback": screen(
                "COMMAND FAILED",
                "Light restored",
                "Provider rejected · rolled back to 72%",
                ("Retry", "home.retry", "light"),
                ("Front door", "home.lock", "confirm"),
            ),
            "confirm": screen(
                "TRUSTED REVIEW",
                "Unlock front door?",
                "Hazardous action · identity required",
                ("Confirm unlock", "home.confirm", "unlocked"),
                ("Cancel", "home.cancel", "light"),
            ),
            "unlocked": screen(
                "FRONT DOOR",
                "Unlocked ✓",
                "Acknowledged once · audit #204",
                ("Lock again", "home.relock", "light"),
                ("Home", "home.home", "home"),
            ),
        },
    },
    "sports": {
        "initial": ("sports.primary", "live"),
        "screens": {
            "live": screen(
                "TOP 8TH",
                "SF 3 · LA 2",
                "1 out · runners on 1st and 2nd",
                ("Replay burst", "sports.burst", "burst"),
                ("Unfollow", "sports.unfollow", "final"),
            ),
            "burst": screen(
                "SCORE UPDATE",
                "SF 5 · LA 2",
                "3 events coalesced · latest revision 44",
                ("End game", "sports.end", "final"),
                ("Live view", "sports.live", "live"),
            ),
            "final": screen(
                "FINAL",
                "SF 5 · LA 3",
                "Ongoing card ended · one haptic",
                ("Scoring plays", "sports.plays", "timeline"),
                ("Follow rematch", "sports.follow", "live"),
            ),
            "timeline": screen(
                "SCORING PLAYS",
                "5 runs · 4 plays",
                "8th: Lee doubled · two scored",
                ("Final score", "sports.final", "final"),
                ("Home", "sports.home", "home"),
            ),
        },
    },
    "wallet": {
        "initial": ("wallet.primary", "pass"),
        "screens": {
            "pass": screen(
                "SFO → JFK",
                "BOARDING 8:10",
                "Gate B12 · Seat 18A · offline ready",
                ("Show QR", "wallet.qr", "qr"),
                ("Test bad update", "wallet.bad", "rejected"),
            ),
            "qr": screen(
                "BOARDING PASS",
                "█ ▄█ █▄ █",
                "Brightness 100% · expires 8:25",
                ("Done", "wallet.done", "pass"),
                ("Bad update", "wallet.bad", "rejected"),
            ),
            "rejected": screen(
                "UPDATE REJECTED",
                "Signature invalid",
                "Last verified pass preserved",
                ("Use safe pass", "wallet.safe", "pass"),
                ("Review details", "wallet.review", "review"),
            ),
            "review": screen(
                "UPDATE REVIEW",
                "Issuer mismatch",
                "Mock Air ≠ signed Doodad Air",
                ("Reject", "wallet.reject", "rejected"),
                ("Home", "wallet.home", "home"),
            ),
        },
    },
    "remote-control": {
        "initial": ("remote-control.primary", "targets"),
        "screens": {
            "targets": screen(
                "PHONE CONNECTED",
                "3 controls",
                "Find phone · Camera · Slides",
                ("Camera shutter", "remote.camera", "pending"),
                ("Disconnect", "remote.disconnect", "offline"),
            ),
            "pending": screen(
                "CAMERA",
                "Sending #73",
                "Tap locked until acknowledgement",
                ("Deliver ack", "remote.ack", "done"),
                ("Lose link", "remote.disconnect", "offline"),
            ),
            "done": screen(
                "CAMERA",
                "Captured ✓",
                "Command #73 applied exactly once",
                ("Capture again", "remote.camera", "pending"),
                ("Targets", "remote.targets", "targets"),
            ),
            "offline": screen(
                "DISCONNECTED",
                "No action sent",
                "Retry ledger preserved · discovery active",
                ("Reconnect", "remote.reconnect", "targets"),
                ("Home", "remote.home", "home"),
            ),
        },
    },
}

BOUND_PROVIDERS: dict[str, tuple[str, str, str]] = {
    "calendar": ("request_calendar", "calendar.sync", "calendar"),
    "voice-notes": ("request_audio", "audio.capture", "audio"),
    "medication": (
        "request_medication",
        "medication.schedule",
        "medication",
    ),
    "sensor-recorder": ("request_sensor", "sensor.record", "sensor"),
    "sleep": ("request_sleep", "sleep.track", "sleep"),
    "media": ("request_media", "media.remote", "media"),
    "navigation": (
        "request_navigation",
        "navigation.route",
        "navigation",
    ),
    "transit": ("request_transit", "transit.read", "transit"),
    "smart-home": ("request_home", "home.control", "home"),
    "sports": ("request_sports", "sports.read", "sports"),
    "wallet": ("request_wallet", "wallet.read", "wallet"),
    "remote-control": (
        "request_remote",
        "remote.control",
        "remote",
    ),
}


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def appspec(slug: str, key: str, definition: dict[str, Any]) -> dict[str, Any]:
    children: list[dict[str, Any]] = [
        {
            "id": f"{slug}.{key}.heading",
            "type": "text",
            "props": {
                "text": definition["heading"],
                "style": "label",
                "max_lines": 1,
            },
        },
        {
            "id": f"{slug}.{key}.summary",
            "type": "text",
            "props": {
                "text": definition["summary"],
                "style": "numeral",
                "max_lines": 2,
                "align": "center",
            },
            "semantics": {
                "label": definition["heading"],
                "value": definition["summary"],
            },
        },
        {
            "id": f"{slug}.{key}.detail",
            "type": "card",
            "props": {
                "title": "Deterministic fixture",
                "body": definition["body"],
                "tone": "neutral",
            },
        },
    ]
    for suffix, action in (
        ("primary", definition["primary"]),
        ("secondary", definition["secondary"]),
    ):
        if action is None:
            continue
        label, action_id, _target = action
        children.append(
            {
                "id": f"{slug}.{key}.{suffix}",
                "type": "button",
                "props": {
                    "label": label,
                    "tone": "primary" if suffix == "primary" else "neutral",
                    "variant": "filled" if suffix == "primary" else "outlined",
                    "size": "default",
                },
                "events": {"tap": action_id},
                "semantics": {"label": label},
            }
        )
    return {
        "schema_version": 1,
        "app_id": slug,
        "screen": {
            "id": f"{slug}.{key}",
            "type": "screen",
            "props": {
                "gap": "sm",
                "align": "stretch",
                "children": children,
            },
        },
    }


def rust_name(value: str) -> str:
    return re.sub(r"[^A-Z0-9]", "_", value.upper())


def rust_source(slug: str, flow: dict[str, Any]) -> str:
    request_function, _, _ = BOUND_PROVIDERS[slug]
    constants = ['const HOME: &[u8] = include_bytes!("../appspec.cbor");']
    for key in flow["screens"]:
        constants.append(
            f'const {rust_name(key)}: &[u8] = '
            f'include_bytes!("../screens/{key}.cbor");'
        )
    transitions: list[tuple[str, str]] = [
        (flow["initial"][0], flow["initial"][1])
    ]
    for definition in flow["screens"].values():
        for field in ("primary", "secondary"):
            action = definition[field]
            if action is not None:
                transitions.append((action[1], action[2]))
    match_arms = []
    for action, target in dict(transitions).items():
        const = "HOME" if target == "home" else rust_name(target)
        match_arms.append(f'        "{action}" => Some({const}),')
    return f"""#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::{{decode_ui_event, mount_appspec, {request_function}}};

{chr(10).join(constants)}

fn target_for(action_id: &str) -> Option<&'static [u8]> {{
    match action_id {{
{chr(10).join(match_arms)}
        _ => None,
    }}
}}

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {{
    let _ = mount_appspec(HOME);
}}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(
    pointer: *const u8,
    length: u32,
) -> u64 {{
    if pointer.is_null() || length == 0 || length > 512 {{
        return 0;
    }}
    let bytes = unsafe {{
        core::slice::from_raw_parts(pointer, length as usize)
    }};
    let event = match decode_ui_event(bytes) {{
        Ok(value) => value,
        Err(_) => return 0,
    }};
    let target = match target_for(event.action_id) {{
        Some(value) => value,
        None => return 0,
    }};
    // Each package imports only its domain-scoped mocked capability.
    if {request_function}(event.action_id, &[]).is_err() {{
        return 0;
    }}
    let _ = mount_appspec(target);
    0
}}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {{
    loop {{
        core::hint::spin_loop();
    }}
}}
"""


def main() -> None:
    for slug, flow in FLOWS.items():
        _, capability, provider = BOUND_PROVIDERS[slug]
        directory = ROOT / "apps" / slug
        screens = directory / "screens"
        screens.mkdir(parents=True, exist_ok=True)
        for key, definition in flow["screens"].items():
            document = appspec(slug, key, definition)
            validate_appspec(document)
            write_json(screens / f"{key}.json", document)
            (screens / f"{key}.cbor").write_bytes(
                compile_canonical_cbor(document)
            )
        (directory / "src" / "lib.rs").write_text(
            rust_source(slug, flow)
        )

        manifest = json.loads((directory / "manifest.json").read_text())
        manifest["capabilities"] = ["ui.mount", capability]
        write_json(directory / "manifest.json", manifest)

        package = json.loads((directory / "package.json").read_text())
        package["implementation_status"] = "interactive-mock-flow"
        package["screen_count"] = 1 + len(flow["screens"])
        package["provider"] = capability
        write_json(directory / "package.json", package)

        readme_lines = [
            f"# {manifest['name']}",
            "",
            "Deterministic interactive conformance package.",
            "",
            "Screens:",
            "",
            "- package launch screen",
        ]
        readme_lines.extend(
            f"- {definition['heading']}: {definition['summary']}"
            for definition in flow["screens"].values()
        )
        readme_lines.extend(
            [
                "",
                f"Every transition crosses the domain-scoped mocked {provider} "
                "capability before mounting the next bounded AppSpec.",
                "",
            ]
        )
        (directory / "README.md").write_text(
            "\n".join(readme_lines)
        )


if __name__ == "__main__":
    main()
