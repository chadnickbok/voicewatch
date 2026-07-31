from __future__ import annotations

import json
import importlib.util
import re
import shutil
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
        "wasm",
        "ui",
    }
    unknown = set(manifest) - allowed
    if unknown:
        raise DoodadError(f"{path} contains unknown fields: {sorted(unknown)}")

    required = allowed - {"ui"}
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

    source_ui: Path | None = None
    if "ui" in manifest:
        source_ui = app_dir / "ui.json"
        if not source_ui.is_file():
            raise DoodadError(f"manifest declares ui.json but {source_ui} is missing")
        from .ui import validate_ui

        validate_ui(read_json(source_ui))

    package_name = cargo_package_name(app_dir)
    command = [
        "cargo",
        "build",
        "--locked",
        "--release",
        "--target",
        "wasm32-unknown-unknown",
        "--package",
        package_name,
    ]
    result = subprocess.run(command, cwd=project_root, check=False)
    if result.returncode != 0:
        raise DoodadError(f"guest build failed with exit code {result.returncode}")

    artifact_name = package_name.replace("-", "_") + ".wasm"
    built_wasm = (
        project_root
        / "target"
        / "wasm32-unknown-unknown"
        / "release"
        / artifact_name
    )
    if not built_wasm.is_file():
        raise DoodadError(f"Cargo did not produce expected artifact {built_wasm}")
    validate_module(project_root, built_wasm, manifest, abi)

    staging_root = (project_root / "target" / "doodad").resolve()
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

    staged_ui: Path | None = None
    if source_ui is not None:
        staged_ui = staging / "ui.json"
        shutil.copy2(source_ui, staged_ui)

    return ProjectPaths(
        root=project_root,
        app=app_dir,
        manifest=staged_manifest,
        ui=staged_ui,
        wasm=staged_wasm,
        staging=staging,
    )
