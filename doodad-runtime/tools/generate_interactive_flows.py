#!/usr/bin/env python3
"""Generate deterministic multi-screen interaction fixtures.

These packages exercise the real AppSpec/Wasm/host capability path while their
external integrations are still deterministic. Product-specific state
 machines (Timer, Weather, Notifications, Tasks, Calculator, Calendar,
 Calories, Workout, Voice Notes, Medication, Sensor Recorder, Sleep, Media,
 Navigation, Transit, Smart Home, Sports, Wallet, Remote Control, and Snake)
 live outside this generator.
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


FLOWS: dict[str, dict[str, Any]] = {}

BOUND_PROVIDERS: dict[str, tuple[str, str, str]] = {
    "calendar": ("request_calendar", "calendar.sync", "calendar"),
    "voice-notes": ("request_audio", "audio.capture", "audio"),
    "medication": (
        "request_medication",
        "medication.schedule",
        "medication",
    ),
    "sensor-recorder": ("request_sensor", "sensor.record", "sensor"),
    "media": ("request_media", "media.remote", "media"),
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
