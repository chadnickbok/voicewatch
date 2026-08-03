#!/usr/bin/env python3
"""Generate the mechanical baseline files for the 20-app conformance suite.

Product-specific AppSpecs are preserved when present. Package manifests,
canonical device bytes, Wasm entrypoints, cross-surface fixtures, and baseline
scenarios are regenerated deterministically from this catalog.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from doodad_cli.appspec import validate_appspec
from doodad_cli.appspec_cbor import _encode, compile_canonical_cbor


ROOT = Path(__file__).resolve().parents[1]

APPS = (
    ("timer", "Timer Suite", "hybrid", "AGCNOV", "1:00 remaining", "Start timer"),
    ("weather", "Weather + Rain", "hybrid", "AGCNV", "72° and clear", "Refresh"),
    ("notifications", "Notification Inbox", "trusted", "AGNV", "2 unread", "Quick reply"),
    ("tasks", "Tasks + Reminders", "generated", "AGCNV", "3 tasks today", "Add task"),
    ("calculator", "Calculator", "generated", "AV", "42", "Calculate"),
    ("calendar", "Calendar + Agenda", "hybrid", "AGCNV", "Design review at 2", "Open event"),
    ("workout", "Workout Tracker", "generated", "AGCNOV", "Set 3 of 5", "Complete set"),
    ("calories", "Nutrition + Hydration", "generated", "AGCNV", "1,420 of 2,000", "Quick add"),
    ("voice-notes", "Voice Notes", "trusted", "AGNOV", "Recording ready", "Record note"),
    ("medication", "Medication + Habits", "generated", "AGCNV", "Vitamin D at 9", "Mark taken"),
    ("sensor-recorder", "Sensor Recorder", "trusted", "AGCNOV", "1,024 samples", "Start recording"),
    ("sleep", "Sleep + Smart Alarm", "trusted", "AGCNOV", "7h 42m", "Start sleep"),
    ("media", "Media Remote", "hybrid", "AGCNOV", "Now playing", "Play"),
    ("navigation", "Navigation", "hybrid", "AGCNOV", "Turn in 200 ft", "Start route"),
    ("transit", "Transit Departures", "generated", "AGCNV", "N in 3 min", "Refresh"),
    ("smart-home", "Smart Home", "generated", "AGCNV", "Living room on", "Toggle light"),
    ("sports", "Live Sports", "generated", "AGCNOV", "SF 3 · LA 2", "Follow game"),
    ("wallet", "Pass + QR Wallet", "trusted", "AGCNV", "Boarding pass ready", "Show pass"),
    ("remote-control", "Remote Control Lab", "trusted", "AGV", "Phone connected", "Trigger"),
    ("snake", "Snake", "generated", "AGCN", "Score 12", "Play Snake"),
)

SURFACE_KEYS = {
    "A": "app",
    "G": "glance",
    "C": "complication",
    "N": "notification",
    "O": "ongoing",
    "V": "voice",
}

INTERACTIVE_FLOW_SLUGS = {
    "calendar",
    "voice-notes",
    "medication",
    "sensor-recorder",
    "sleep",
    "media",
    "navigation",
    "transit",
    "smart-home",
    "sports",
    "wallet",
    "remote-control",
}


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def generic_appspec(slug: str, title: str, summary: str, action: str) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "app_id": slug,
        "screen": {
            "id": f"{slug}.home",
            "type": "screen",
            "props": {
                "gap": "sm",
                "align": "stretch",
                "children": [
                    {
                        "id": f"{slug}.heading",
                        "type": "text",
                        "props": {
                            "text": title.upper(),
                            "style": "label",
                            "max_lines": 1,
                        },
                    },
                    {
                        "id": f"{slug}.summary",
                        "type": "text",
                        "props": {
                            "text": summary,
                            "style": "numeral",
                            "max_lines": 2,
                            "align": "center",
                        },
                        "semantics": {"label": f"{title} status", "value": summary},
                    },
                    {
                        "id": f"{slug}.mock",
                        "type": "card",
                        "props": {
                            "title": "Deterministic provider",
                            "body": "Interactive mock data · revision 1",
                            "tone": "neutral",
                        },
                    },
                    {
                        "id": f"{slug}.primary",
                        "type": "button",
                        "props": {
                            "label": action,
                            "tone": "primary",
                            "variant": "filled",
                            "size": "default",
                        },
                        "events": {"tap": f"{slug}.primary"},
                        "semantics": {"label": action},
                    },
                ],
            },
        },
    }


def nodes(node: dict[str, Any]) -> list[dict[str, Any]]:
    result = [node]
    for child in node.get("props", {}).get("children", []):
        result.extend(nodes(child))
    return result


def update_target(appspec: dict[str, Any]) -> str:
    for node in nodes(appspec["screen"]):
        if node["type"] == "text":
            return str(node["id"])
    raise ValueError(f"{appspec['app_id']} has no text update target")


def source() -> str:
    return """#![no_std]

use core::panic::PanicInfo;
use doodad_sdk::mount_appspec;

const APPSPEC: &[u8] = include_bytes!("../appspec.cbor");
const INTERACTION_COMMANDS: &[u8] = include_bytes!("../interaction.cbor");

#[unsafe(no_mangle)]
pub extern "C" fn app_start() {
    let _ = mount_appspec(APPSPEC);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn handle_event(pointer: *const u8, length: u32) -> u64 {
    if pointer.is_null() || length == 0 || length > 512 {
        return 0;
    }
    // The trusted host has already validated and canonicalized this bounded
    // event envelope. Every baseline package acknowledges interaction with an
    // in-place command; domain-specific state machines replace this command
    // as each app enters its implementation wave.
    let bytes = unsafe { core::slice::from_raw_parts(pointer, length as usize) };
    if !matches!(bytes.first(), Some(&0xa7) | Some(&0xa8)) {
        return 0;
    }
    ((INTERACTION_COMMANDS.as_ptr() as u64) << 32)
        | INTERACTION_COMMANDS.len() as u64
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
"""


def surface_state(
    slug: str,
    title: str,
    surfaces: str,
    summary: str,
    action_label: str,
) -> dict[str, Any]:
    declared = [SURFACE_KEYS[key] for key in surfaces]
    action_id = f"{slug}.primary"
    projections: dict[str, Any] = {
        "app": {
            "revision": 1,
            "screen_id": f"{slug}.home",
            "title": title,
            "summary": summary,
            "state": {"provider": "mock", "interactive": True},
        }
    }
    if "G" in surfaces:
        projections["glance"] = {
            "revision": 1,
            "template": "ongoing" if "O" in surfaces else "metric",
            "title": title,
            "primary": summary,
            "secondary": "Mock data · now",
            "action": {"id": action_id, "label": action_label},
        }
    if "C" in surfaces:
        projections["complication"] = {
            "revision": 1,
            "label": title[:24],
            "value": summary[:24],
            "icon": slug,
        }
    if "N" in surfaces:
        projections["notification"] = {"revision": 1, "status": "inactive"}
    if "O" in surfaces:
        projections["ongoing"] = {
            "revision": 1,
            "status": "active",
            "title": title,
            "detail": summary,
            "actions": [{"id": action_id, "label": action_label}],
        }
    if "V" in surfaces:
        projections["voice"] = {
            "revision": 1,
            "actions": [
                {
                    "id": action_id,
                    "example": action_label,
                    "confirmation": "never",
                }
            ],
        }
    return {
        "schema_version": 1,
        "app_id": f"dev.doodad.{slug}",
        "domain_revision": 1,
        "observed_at_ms": 0,
        "freshness": "current",
        "declared_surfaces": declared,
        "surfaces": projections,
    }


def scenario(slug: str, snapshot: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "id": f"{slug}.baseline.interaction",
        "app_id": f"dev.doodad.{slug}",
        "initial_state": {
            "wall_time_ms": 1700000000000,
            "timezone_offset_minutes": -420,
            "app_state": "foreground",
            "display_state": "awake",
            "connectivity": "online",
        },
        "steps": [
            {
                "op": "provider.emit",
                "provider": "mock",
                "event": f"{slug}.loaded",
                "revision": 1,
                "status": "current",
                "payload": {"deterministic": True},
            },
            {"op": "surface.publish", "snapshot": snapshot},
            {
                "op": "action.dispatch",
                "target": f"{slug}.primary",
                "value": {"source": "touch"},
            },
            {
                "op": "assert.state",
                "equals": {
                    "providers.mock.revision": 1,
                    f"surfaces.dev.doodad.{slug}.domain_revision": 1,
                    "actions.count": 1,
                    "actions.last_target": f"{slug}.primary",
                },
            },
        ],
    }


def main() -> None:
    catalog = []
    for index, (slug, title, mode, surfaces, summary, action) in enumerate(APPS, 1):
        directory = ROOT / "apps" / slug
        directory.mkdir(parents=True, exist_ok=True)
        appspec_path = directory / "appspec.json"
        if appspec_path.exists():
            appspec = json.loads(appspec_path.read_text())
        else:
            appspec = generic_appspec(slug, title, summary, action)
            write_json(appspec_path, appspec)
        validate_appspec(appspec)
        compiled = compile_canonical_cbor(appspec)
        (directory / "appspec.cbor").write_bytes(compiled)
        screens_directory = directory / "screens"
        if screens_directory.is_dir():
            for screen_path in sorted(screens_directory.glob("*.json")):
                screen = json.loads(screen_path.read_text())
                validate_appspec(screen)
                screen_path.with_suffix(".cbor").write_bytes(
                    compile_canonical_cbor(screen)
                )

        command = {
            0: 1,
            1: [
                {
                    0: 0,
                    1: update_target(appspec),
                    2: 0,
                    3: f"{title}: action received",
                }
            ],
        }
        (directory / "interaction.cbor").write_bytes(_encode(command))
        capabilities = ["ui.mount"]
        if slug == "timer":
            capabilities.extend(
                ["timer.schedule", "timer.cancel", "timer.acknowledge"]
            )
        elif slug == "weather":
            capabilities.append("weather.read")
        elif slug in INTERACTIVE_FLOW_SLUGS:
            capabilities.append(
                {
                    "calendar": "calendar.sync",
                    "voice-notes": "audio.capture",
                    "medication": "medication.schedule",
                    "sensor-recorder": "sensor.record",
                    "sleep": "sleep.track",
                    "media": "media.remote",
                    "navigation": "navigation.route",
                    "transit": "transit.read",
                    "smart-home": "home.control",
                    "sports": "sports.read",
                    "wallet": "wallet.read",
                    "remote-control": "remote.control",
                }[slug]
            )
        elif slug == "workout":
            capabilities.append("workout.store")
        elif slug == "snake":
            capabilities.append("game.clock")
        manifest_path = directory / "manifest.json"
        existing_assets = []
        if manifest_path.is_file():
            existing_assets = json.loads(
                manifest_path.read_text(encoding="utf-8")
            ).get("assets", [])
        manifest = {
            "schema_version": 1,
            "id": f"dev.doodad.{slug}",
            "name": title,
            "version": "0.1.0",
            "host_abi": 1,
            "capabilities": capabilities,
            "wasm": "app.wasm",
        }
        if existing_assets:
            manifest["assets"] = existing_assets
        write_json(manifest_path, manifest)
        write_json(
            directory / "package.json",
            {
                "schema_version": 1,
                "suite_index": index,
                "mode": mode,
                "surfaces": [SURFACE_KEYS[key] for key in surfaces],
                "mock_provider": "deterministic",
                "implementation_status": (
                    "wave1-functional"
                    if slug in {
                        "timer",
                        "weather",
                        "notifications",
                        "tasks",
                        "calculator",
                        "calendar",
                    }
                    else "wave2-functional"
                    if slug in {"workout", "calories"}
                    else "wave4-functional"
                    if slug == "snake"
                    else "interactive-mock-flow"
                    if slug in INTERACTIVE_FLOW_SLUGS
                    else "package-baseline"
                ),
            },
        )
        write_json(
            directory / "surfaces" / "baseline.surface.json",
            surface_state(slug, title, surfaces, summary, action),
        )
        snapshot = json.loads(
            (directory / "surfaces" / "baseline.surface.json").read_text()
        )
        write_json(
            directory / "scenarios" / "baseline.scenario.json",
            scenario(slug, snapshot),
        )
        (directory / "Cargo.toml").write_text(
            f"""[package]
name = "doodad-{slug}"
version = "0.1.0"
edition = "2024"
license = "MIT"
publish = false

[lib]
crate-type = ["cdylib"]
bench = false
doctest = false
test = false

[dependencies]
doodad-sdk = {{ path = "../../sdk/rust/doodad-sdk" }}
"""
        )
        source_path = directory / "src" / "lib.rs"
        source_path.parent.mkdir(parents=True, exist_ok=True)
        if not source_path.exists():
            source_path.write_text(source())
        catalog.append(
            {
                "index": index,
                "slug": slug,
                "id": f"dev.doodad.{slug}",
                "name": title,
                "mode": mode,
                "surfaces": [SURFACE_KEYS[key] for key in surfaces],
            }
        )
    write_json(
        ROOT / "apps" / "conformance-suite.json",
        {"schema_version": 1, "apps": catalog},
    )


if __name__ == "__main__":
    main()
