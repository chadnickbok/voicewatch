from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
import webbrowser
from pathlib import Path
from typing import Any

from .appspec import validate_appspec
from .appspec_cbor import compile_canonical_cbor
from .contract import (
    DoodadError,
    ProjectPaths,
    build_and_stage,
    find_project_root,
    inspect_module,
    read_json,
    resolve_app,
)
from .native import NativeHost
from .server import PreviewState, start_server
from .ui import render_ui, validate_ui


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        prog="doodad",
        description="Build, validate, and preview Doodad app packages.",
    )
    subcommands = result.add_subparsers(dest="command", required=True)

    subcommands.add_parser("doctor", help="check local simulator dependencies")

    inspect_parser = subcommands.add_parser(
        "inspect", help="show Wasm imports, exports, and memory"
    )
    inspect_parser.add_argument("wasm")

    for name, help_text in (
        ("build", "build and stage an app package"),
        ("check", "build, validate, and execute with WAMR"),
        ("test", "run the headless package smoke test"),
    ):
        command_parser = subcommands.add_parser(name, help=help_text)
        command_parser.add_argument("app")

    dev_parser = subcommands.add_parser(
        "dev", help="watch an app and open the LVGL browser preview"
    )
    dev_parser.add_argument("app")
    dev_parser.add_argument("--port", type=int, default=8765)
    dev_parser.add_argument("--no-open", action="store_true")

    catalog_parser = subcommands.add_parser(
        "catalog", help="render a deterministic Material component story"
    )
    catalog_parser.add_argument(
        "--story",
        choices=(
            "foundation",
            "stress",
            "components",
            "calories",
            "calculator",
            "workout",
            "inputs",
            "voice",
            "navigation",
            "system",
            "transforming-list",
            "expressive-depth",
            "mockup-hydration",
            "mockup-focus",
            "mockup-travel",
            "mockup-music",
        ),
        default="foundation",
    )
    catalog_parser.add_argument(
        "--output", type=Path, default=Path("target/catalog/foundation.bmp")
    )
    appspec_parser = subcommands.add_parser(
        "appspec", help="validate and preview a semantic AppSpec v1 screen"
    )
    appspec_parser.add_argument("file", type=Path)
    appspec_parser.add_argument(
        "--output", type=Path, default=Path("target/appspec/preview.bmp")
    )
    appspec_parser.add_argument("--validate-only", action="store_true")
    return result


def main(arguments: list[str] | None = None) -> int:
    options = parser().parse_args(arguments)
    try:
        root = find_project_root(Path(__file__).resolve().parent)
        if options.command == "doctor":
            return doctor(root)
        if options.command == "inspect":
            return inspect_command(root, Path(options.wasm).resolve())
        if options.command == "catalog":
            output = options.output.resolve()
            with NativeHost(root) as host:
                host.show_catalog(
                    {
                        "foundation": 0,
                        "stress": 1,
                        "components": 2,
                        "calories": 3,
                        "calculator": 4,
                        "workout": 5,
                        "inputs": 6,
                        "voice": 7,
                        "navigation": 8,
                        "system": 9,
                        "transforming-list": 10,
                        "expressive-depth": 11,
                        "mockup-hydration": 12,
                        "mockup-focus": 13,
                        "mockup-travel": 14,
                        "mockup-music": 15,
                    }[options.story]
                )
                host.write_bmp(output)
            print(f"catalog passed: {options.story}\npreview: {output}")
            return 0
        if options.command == "appspec":
            document = read_json(options.file.resolve())
            stats = validate_appspec(document)
            if options.validate_only:
                print(
                    f"AppSpec valid: {document['app_id']} "
                    f"({stats.nodes} nodes, depth {stats.maximum_depth})"
                )
                return 0
            compiled = compile_canonical_cbor(document)
            output = options.output.resolve()
            with NativeHost(root) as host:
                host.show_appspec(compiled)
                host.write_bmp(output)
            print(
                f"AppSpec passed: {document['app_id']}\n"
                f"nodes: {stats.nodes}; depth: {stats.maximum_depth}\n"
                f"preview: {output}"
            )
            return 0
        app = resolve_app(root, options.app)
        if options.command == "build":
            package = build_and_stage(root, app)
            validate_staged_ui(package)
            print(package.staging)
            return 0
        if options.command in {"check", "test"}:
            package = build_and_stage(root, app)
            frame = package.staging / "preview.bmp"
            with load_package(package) as host:
                if options.command == "test":
                    host.click_first_action()
                host.write_bmp(frame)
            if options.command == "test":
                smoke_test_frame(frame)
            print(
                f"{options.command} passed: {package.manifest.parent}\n"
                f"preview: {frame}"
            )
            return 0
        if options.command == "dev":
            return dev(root, app, options.port, options.no_open)
    except (DoodadError, OSError) as error:
        print(f"doodad: {error}", file=sys.stderr)
        return 1
    return 2


def doctor(root: Path) -> int:
    checks = {
        "python": sys.version.split()[0],
        "cargo": command_version(["cargo", "--version"]),
        "cmake": command_version(["cmake", "--version"]),
        "ninja": shutil.which("ninja") or "missing",
        "WAMR source": str(
            root
            / "firmware"
            / "managed_components"
            / "espressif__wasm-micro-runtime"
        ),
        "LVGL source": str(
            root / "firmware" / "managed_components" / "lvgl__lvgl"
        ),
    }
    missing = []
    for name, value in checks.items():
        okay = value != "missing" and (
            not name.endswith("source") or Path(value).is_dir()
        )
        print(f"{'ok' if okay else 'missing':7} {name}: {value}")
        if not okay:
            missing.append(name)
    if missing:
        raise DoodadError(f"missing dependencies: {', '.join(missing)}")
    NativeHost(root).close()
    print("ok      native host: WAMR 2.4.0 + LVGL 9.5.0")
    return 0


def command_version(command: list[str]) -> str:
    executable = shutil.which(command[0])
    if executable is None:
        return "missing"
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    return (completed.stdout or completed.stderr).splitlines()[0]


def inspect_command(root: Path, wasm: Path) -> int:
    imports, exports, memories, signatures = inspect_module(root, wasm)
    for module, name, kind in imports:
        print(f"import {module}.{name} ({kind})")
    for name, kind in exports:
        print(f"export {name} ({kind})")
        if kind == "function":
            params, results = signatures[name]
            print(
                f"  signature ({', '.join(params)})"
                f" -> ({', '.join(results)})"
            )
    for minimum, maximum in memories:
        print(f"memory minimum={minimum} page(s), maximum={maximum} page(s)")
    return 0


def validate_staged_ui(package: ProjectPaths) -> dict[str, Any] | None:
    if package.ui is None:
        return None
    document = read_json(package.ui)
    validate_ui(document)
    return document


def load_package(package: ProjectPaths) -> NativeHost:
    document = validate_staged_ui(package)
    host = NativeHost(package.root)
    try:
        host.start_wasm(package.wasm)
        if document is not None:
            render_ui(host, document)
        return host
    except BaseException:
        host.close()
        raise


def smoke_test_frame(frame: Path) -> None:
    payload = frame.read_bytes()
    expected_size = 54 + 240 * 240 * 3
    if len(payload) != expected_size or payload[:2] != b"BM":
        raise DoodadError("headless renderer did not produce a 240x240 BMP")
    if len(set(payload[54:])) < 4:
        raise DoodadError("headless renderer produced an empty-looking frame")


def watch_snapshot(root: Path, app: Path) -> tuple[tuple[str, int, int], ...]:
    watched_roots = [app, root / "sdk" / "rust", root / "contracts", root / "ui"]
    entries = []
    for watched in watched_roots:
        for path in watched.rglob("*"):
            if path.is_file() and path.suffix in {".rs", ".toml", ".json", ".c", ".h"}:
                stat = path.stat()
                entries.append((str(path), stat.st_mtime_ns, stat.st_size))
    return tuple(sorted(entries))


def dev(root: Path, app: Path, port: int, no_open: bool) -> int:
    runtime_dir = root / "target" / "doodad-dev"
    runtime_dir.mkdir(parents=True, exist_ok=True)
    frame = runtime_dir / "frame.bmp"
    state = PreviewState(frame)
    server, thread = start_server(root / "tools" / "web", state, port)
    url = f"http://127.0.0.1:{port}/"
    print(f"doodad dev: {url}")
    if not no_open:
        webbrowser.open(url)

    current_host: NativeHost | None = None
    revision = 0
    previous_snapshot: tuple[tuple[str, int, int], ...] | None = None
    try:
        while True:
            snapshot = watch_snapshot(root, app)
            if snapshot != previous_snapshot:
                previous_snapshot = snapshot
                try:
                    package = build_and_stage(root, app)
                    validate_staged_ui(package)
                    if current_host is not None:
                        current_host.close()
                        current_host = None
                    replacement = load_package(package)
                    try:
                        replacement.write_bmp(frame)
                    except BaseException:
                        replacement.close()
                        raise
                    current_host = replacement
                    revision += 1
                    manifest = read_json(package.manifest)
                    state.update(
                        revision=revision,
                        status="running",
                        message="Package is running",
                        stale=False,
                        app={"id": manifest["id"], "name": manifest["name"]},
                        package=str(package.staging),
                    )
                    print(f"[{revision}] ready: {manifest['id']}")
                except (DoodadError, OSError) as error:
                    state.update(
                        status="error",
                        message=str(error),
                        stale=frame.is_file(),
                    )
                    print(f"reload failed: {error}", file=sys.stderr)
            time.sleep(0.35)
    except KeyboardInterrupt:
        print("\ndoodad dev stopped")
        return 0
    finally:
        if current_host is not None:
            current_host.close()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
