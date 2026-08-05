from __future__ import annotations

import json
import hashlib
import importlib.util
import re
import shutil
import struct
import subprocess
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class DoodadError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProjectPaths:
    root: Path
    app: Path
    manifest: Path
    ui: Path | None
    wasm: Path
    staging: Path


ID_PATTERN = re.compile(r"^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+$")
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$")
CAPABILITY_PATTERN = re.compile(r"^[a-z][a-z0-9]*(\.[a-z][a-z0-9]*)+$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ASSET_MEDIA_TYPE = "image/vnd.doodad.rgb565le"
IDENTITY_FIELDS = {"icon", "theme_seed"}
IDENTITY_ICONS = {
    "generic",
    "timer",
    "weather",
    "tasks",
    "calculator",
    "calendar",
    "water_drop",
}
THEME_SEED_PATTERN = re.compile(r"^#[0-9A-F]{6}$")
ASSET_FIELDS = {
    "sha256",
    "path",
    "media_type",
    "width",
    "height",
    "encoded_bytes",
    "decoded_bytes",
}


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise DoodadError(f"missing required file: {path}") from error
    except (OSError, json.JSONDecodeError) as error:
        raise DoodadError(f"cannot read {path}: {error}") from error
    if not isinstance(value, dict):
        raise DoodadError(f"{path} must contain a JSON object")
    return value


def find_project_root(start: Path) -> Path:
    current = start.resolve()
    for candidate in (current, *current.parents):
        if (candidate / "Cargo.toml").is_file() and (candidate / "contracts").is_dir():
            return candidate
    raise DoodadError("run this command from inside the doodad-runtime project")


def resolve_app(project_root: Path, app_argument: str) -> Path:
    candidate = Path(app_argument)
    if not candidate.is_absolute():
        candidate = (Path.cwd() / candidate).resolve()
    if candidate.is_dir():
        return candidate
    fallback = (project_root / "apps" / app_argument).resolve()
    if fallback.is_dir():
        return fallback
    raise DoodadError(f"app project not found: {app_argument}")


def load_abi(project_root: Path) -> dict[str, Any]:
    abi = read_json(project_root / "contracts" / "abi" / "v1.json")
    if abi.get("schema_version") != 1 or abi.get("host_abi") != 1:
        raise DoodadError("contracts/abi/v1.json has an unsupported version")
    return abi


def validate_manifest(
    manifest: dict[str, Any], abi: dict[str, Any], path: Path
) -> None:
    allowed = {
        "schema_version",
        "id",
        "name",
        "version",
        "host_abi",
        "capabilities",
        "identity",
        "wasm",
        "ui",
        "assets",
    }
    unknown = set(manifest) - allowed
    if unknown:
        raise DoodadError(f"{path} contains unknown fields: {sorted(unknown)}")

    required = allowed - {"ui", "assets"}
    missing = required - set(manifest)
    if missing:
        raise DoodadError(f"{path} is missing fields: {sorted(missing)}")
    if manifest["schema_version"] != 1:
        raise DoodadError("manifest schema_version must be 1")
    if (
        not isinstance(manifest["id"], str)
        or len(manifest["id"]) > 96
        or not ID_PATTERN.fullmatch(manifest["id"])
    ):
        raise DoodadError("manifest id must be a reverse-domain lowercase identifier")
    if not isinstance(manifest["name"], str) or not 1 <= len(manifest["name"]) <= 48:
        raise DoodadError("manifest name must contain 1..48 characters")
    if (
        not isinstance(manifest["version"], str)
        or not VERSION_PATTERN.fullmatch(manifest["version"])
    ):
        raise DoodadError("manifest version must be a semantic version")
    if manifest["host_abi"] != abi["host_abi"]:
        raise DoodadError(
            f"app requires host ABI {manifest['host_abi']}; this tool implements "
            f"{abi['host_abi']}"
        )
    if manifest["wasm"] != "app.wasm":
        raise DoodadError("manifest wasm filename must be app.wasm")
    if "ui" in manifest and manifest["ui"] != "ui.json":
        raise DoodadError("manifest ui filename must be ui.json")

    identity = manifest["identity"]
    if not isinstance(identity, dict) or set(identity) != IDENTITY_FIELDS:
        raise DoodadError(
            "manifest identity must contain exactly icon and theme_seed"
        )
    if (
        not isinstance(identity["icon"], str)
        or identity["icon"] not in IDENTITY_ICONS
    ):
        raise DoodadError("manifest identity icon is not in the curated icon set")
    if (
        not isinstance(identity["theme_seed"], str)
        or THEME_SEED_PATTERN.fullmatch(identity["theme_seed"]) is None
    ):
        raise DoodadError("manifest identity theme_seed must be uppercase #RRGGBB")

    capabilities = manifest["capabilities"]
    if not isinstance(capabilities, list):
        raise DoodadError("manifest capabilities must be a unique array")
    if not all(isinstance(capability, str) for capability in capabilities):
        raise DoodadError("manifest capabilities must contain only strings")
    if len(set(capabilities)) != len(capabilities):
        raise DoodadError("manifest capabilities must be a unique array")
    known = set(abi["capabilities"])
    for capability in capabilities:
        if not CAPABILITY_PATTERN.fullmatch(capability):
            raise DoodadError(f"invalid capability identifier: {capability!r}")
        if capability not in known:
            raise DoodadError(f"host ABI v1 does not define capability {capability!r}")

    assets = manifest.get("assets", [])
    if (
        not isinstance(assets, list)
        or len(assets) > 16
        or any(not isinstance(asset, dict) for asset in assets)
    ):
        raise DoodadError("manifest assets must contain at most 16 objects")
    asset_hashes: set[str] = set()
    for index, asset in enumerate(assets):
        if set(asset) != ASSET_FIELDS:
            raise DoodadError(
                f"manifest assets[{index}] must contain exactly "
                f"{sorted(ASSET_FIELDS)}"
            )
        digest = asset["sha256"]
        if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
            raise DoodadError(
                f"manifest assets[{index}].sha256 must be a lowercase SHA-256 digest"
            )
        if digest in asset_hashes:
            raise DoodadError("manifest asset hashes must be unique")
        asset_hashes.add(digest)
        expected_path = f"assets/{digest}.dimg"
        if asset["path"] != expected_path:
            raise DoodadError(
                f"manifest assets[{index}].path must be {expected_path}"
            )
        if asset["media_type"] != ASSET_MEDIA_TYPE:
            raise DoodadError(
                f"manifest assets[{index}].media_type is unsupported"
            )
        width = asset["width"]
        height = asset["height"]
        encoded_bytes = asset["encoded_bytes"]
        decoded_bytes = asset["decoded_bytes"]
        if (
            any(
                isinstance(value, bool) or not isinstance(value, int)
                for value in (width, height, encoded_bytes, decoded_bytes)
            )
            or not 1 <= width <= 512
            or not 1 <= height <= 512
            or decoded_bytes != width * height * 2
            or encoded_bytes != decoded_bytes + 12
            or encoded_bytes > 512 * 1024
        ):
            raise DoodadError(f"manifest assets[{index}] has invalid dimensions")


def _validated_asset_payload(app_dir: Path, asset: dict[str, Any]) -> Path:
    relative_path = Path(asset["path"])
    source = (app_dir / relative_path).resolve()
    app_root = app_dir.resolve()
    if source.parent.parent != app_root or source.parent.name != "assets":
        raise DoodadError(f"unsafe package asset path {relative_path}")
    try:
        payload = source.read_bytes()
    except OSError as error:
        raise DoodadError(f"cannot read package asset {source}: {error}") from error
    if len(payload) != asset["encoded_bytes"]:
        raise DoodadError(f"package asset {source} has an unexpected size")
    if hashlib.sha256(payload).hexdigest() != asset["sha256"]:
        raise DoodadError(f"package asset {source} does not match its content hash")
    try:
        magic, width, height, pixel_format, flags, reserved = struct.unpack(
            "<4sHHBBH", payload[:12]
        )
    except struct.error as error:
        raise DoodadError(f"package asset {source} has a truncated header") from error
    if (
        magic != b"DIMG"
        or width != asset["width"]
        or height != asset["height"]
        or pixel_format != 1
        or flags != 0
        or reserved != 0
    ):
        raise DoodadError(f"package asset {source} has an invalid DIMG header")
    return source


def cargo_package_name(app_dir: Path) -> str:
    cargo_path = app_dir / "Cargo.toml"
    try:
        cargo = tomllib.loads(cargo_path.read_text(encoding="utf-8"))
        return str(cargo["package"]["name"])
    except (FileNotFoundError, OSError, tomllib.TOMLDecodeError, KeyError) as error:
        raise DoodadError(f"cannot read Rust package metadata from {cargo_path}") from error


def inspect_module(
    project_root: Path, wasm: Path
) -> tuple[list[Any], list[Any], list[Any], dict[str, Any]]:
    script = project_root / "scripts" / "inspect-wasm.py"
    spec = importlib.util.spec_from_file_location("doodad_inspect_wasm", script)
    if spec is None or spec.loader is None:
        raise DoodadError(f"cannot load WebAssembly inspector: {script}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    try:
        return module.inspect(wasm)
    except (OSError, module.WasmFormatError, UnicodeDecodeError) as error:
        raise DoodadError(f"invalid WebAssembly module: {error}") from error


def validate_module(
    project_root: Path,
    wasm: Path,
    manifest: dict[str, Any],
    abi: dict[str, Any],
) -> None:
    maximum_bytes = int(abi["wasm_profile"]["maximum_module_bytes"])
    size = wasm.stat().st_size
    if size <= 0 or size > maximum_bytes:
        raise DoodadError(f"app.wasm size {size} is outside 1..{maximum_bytes} bytes")

    imports, exports, memories, signatures = inspect_module(
        project_root, wasm
    )
    expected_imports = []
    for capability in manifest["capabilities"]:
        definition = abi["capabilities"][capability]
        expected_imports.append(
            (
                definition["import_module"],
                definition["import_name"],
                "function",
            )
        )
    if imports != expected_imports:
        raise DoodadError(
            f"Wasm imports {imports!r} do not match declared capabilities "
            f"{expected_imports!r}"
        )

    required_exports = {
        (abi["entrypoint"]["export"], "function"),
        ("memory", "memory"),
    }
    if "event_handler" in abi:
        required_exports.add(
            (abi["event_handler"]["export"], "function")
        )
    missing = required_exports - set(exports)
    if missing:
        raise DoodadError(f"app.wasm is missing exports: {sorted(missing)}")
    expected_entrypoint_signature = (
        tuple(abi["entrypoint"]["params"]),
        tuple(abi["entrypoint"]["results"]),
    )
    entrypoint = abi["entrypoint"]["export"]
    if signatures.get(entrypoint) != expected_entrypoint_signature:
        raise DoodadError(
            f"{entrypoint} signature {signatures.get(entrypoint)!r} does not "
            f"match {expected_entrypoint_signature!r}"
        )
    if "event_handler" in abi:
        event_handler = abi["event_handler"]
        expected_event_signature = (
            tuple(event_handler["params"]),
            tuple(event_handler["results"]),
        )
        event_export = event_handler["export"]
        if signatures.get(event_export) != expected_event_signature:
            raise DoodadError(
                f"{event_export} signature "
                f"{signatures.get(event_export)!r} does not match "
                f"{expected_event_signature!r}"
            )
    provider_handler = abi.get("provider_event_handler")
    if isinstance(provider_handler, dict):
        prefixes = provider_handler.get(
            "required_when_capability_prefixes", []
        )
        requires_provider_handler = any(
            capability.startswith(prefix)
            for capability in manifest["capabilities"]
            for prefix in prefixes
        )
        if requires_provider_handler:
            provider_export = provider_handler["export"]
            if (provider_export, "function") not in set(exports):
                raise DoodadError(
                    f"app.wasm is missing provider export "
                    f"{provider_export!r}"
                )
            expected_provider_signature = (
                tuple(provider_handler["params"]),
                tuple(provider_handler["results"]),
            )
            if (
                signatures.get(provider_export)
                != expected_provider_signature
            ):
                raise DoodadError(
                    f"{provider_export} signature "
                    f"{signatures.get(provider_export)!r} does not match "
                    f"{expected_provider_signature!r}"
                )
    expected_memory = [
        (
            int(abi["wasm_profile"]["initial_memory_pages"]),
            int(abi["wasm_profile"]["maximum_memory_pages"]),
        )
    ]
    if memories != expected_memory:
        raise DoodadError(
            f"app.wasm memory limits {memories!r} do not match {expected_memory!r}"
        )


def build_and_stage(project_root: Path, app_dir: Path) -> ProjectPaths:
    abi = load_abi(project_root)
    source_manifest_path = app_dir / "manifest.json"
    manifest = read_json(source_manifest_path)
    validate_manifest(manifest, abi, source_manifest_path)
    source_assets = [
        _validated_asset_payload(app_dir, asset)
        for asset in manifest.get("assets", [])
    ]

    source_ui: Path | None = None
    if "ui" in manifest:
        source_ui = app_dir / "ui.json"
        if not source_ui.is_file():
            raise DoodadError(f"manifest declares ui.json but {source_ui} is missing")
        from .ui import validate_ui

        validate_ui(read_json(source_ui))

    package_name = cargo_package_name(app_dir)
    external_app = not app_dir.resolve().is_relative_to(project_root.resolve())
    target_root = app_dir / "target" if external_app else project_root / "target"
    command = ["cargo", "build", "--locked", "--release"]
    if external_app:
        command.extend(
            [
                "--manifest-path",
                str(app_dir / "Cargo.toml"),
                "--target-dir",
                str(target_root),
            ]
        )
    else:
        command.extend(["--package", package_name])
    command.extend(["--target", "wasm32-unknown-unknown"])
    result = subprocess.run(
        command, cwd=app_dir if external_app else project_root, check=False
    )
    if result.returncode != 0:
        raise DoodadError(f"guest build failed with exit code {result.returncode}")

    artifact_name = package_name.replace("-", "_") + ".wasm"
    built_wasm = (
        target_root
        / "wasm32-unknown-unknown"
        / "release"
        / artifact_name
    )
    if not built_wasm.is_file():
        raise DoodadError(f"Cargo did not produce expected artifact {built_wasm}")
    validate_module(project_root, built_wasm, manifest, abi)

    staging_root = (target_root / "doodad").resolve()
    staging = (staging_root / manifest["id"]).resolve()
    if staging.parent != staging_root:
        raise DoodadError("unsafe staging package identifier")
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    staged_manifest = staging / "manifest.json"
    staged_wasm = staging / "app.wasm"
    shutil.copy2(source_manifest_path, staged_manifest)
    shutil.copy2(built_wasm, staged_wasm)
    source_agent = app_dir / "agent.json"
    if source_agent.is_file():
        shutil.copy2(source_agent, staging / "agent.json")

    staged_ui: Path | None = None
    if source_ui is not None:
        staged_ui = staging / "ui.json"
        shutil.copy2(source_ui, staged_ui)

    if source_assets:
        staged_assets = staging / "assets"
        staged_assets.mkdir()
        for source_asset in source_assets:
            shutil.copy2(source_asset, staged_assets / source_asset.name)

    return ProjectPaths(
        root=project_root,
        app=app_dir,
        manifest=staged_manifest,
        ui=staged_ui,
        wasm=staged_wasm,
        staging=staging,
    )
