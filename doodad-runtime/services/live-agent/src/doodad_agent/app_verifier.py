"""Independent deterministic gates for a generated rest-timer package."""

from __future__ import annotations

import hashlib
import json
import os
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource


class VerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class VerifiedArtifact:
    artifact_id: str
    package_path: str
    preview_path: str
    sha256: str
    summary: str
    gates: tuple[str, ...]

    def document(self) -> dict[str, Any]:
        return {
            "artifact_id": self.artifact_id,
            "package_path": self.package_path,
            "preview_path": self.preview_path,
            "sha256": self.sha256,
            "summary": self.summary,
            "gates": list(self.gates),
        }


class RestTimerVerifier:
    ALLOWED_CAPABILITIES = {
        "ui.mount",
        "timer.schedule",
        "timer.cancel",
        "timer.acknowledge",
    }

    def __init__(self, runtime_root: Path, timeout_seconds: int = 600) -> None:
        self.runtime_root = runtime_root.resolve()
        self.timeout_seconds = timeout_seconds

    def verify(self, workspace: Path, layout: str) -> VerifiedArtifact:
        app = workspace / "app"
        manifest = self._read_json(app / "manifest.json")
        appspec = self._read_json(app / "appspec.json")
        agent = self._read_json(app / "agent.json")
        self._validate_schema("manifest-v1.schema.json", manifest)
        self._validate_schema("appspec-v1.schema.json", appspec)
        self._validate_schema("agent-contract-v1.schema.json", agent)
        self._validate_identity(manifest, appspec, agent)
        self._validate_permissions(manifest, agent)
        self._validate_semantics(appspec, layout)
        self._validate_timer_source(app / "src" / "lib.rs")
        self._compile_appspec(appspec, app / "appspec.cbor")

        self._run(
            [
                "cargo",
                "generate-lockfile",
                "--manifest-path",
                str(app / "Cargo.toml"),
            ],
            cwd=app,
        )
        gates = ["schema", "semantics", "permissions", "timer-source"]
        for command in ("build", "check", "test"):
            self._run([str(self.runtime_root / "doodad"), command, str(app)])
            gates.append(command)

        package = app / "target" / "doodad" / str(manifest["id"])
        wasm = package / "app.wasm"
        self._run([str(self.runtime_root / "doodad"), "inspect", str(wasm)])
        gates.append("wasm-inspect")

        scenarios = sorted((app / "scenarios").glob("*.scenario.json"))
        if not scenarios:
            raise VerificationError("generated app has no deterministic timer scenario")
        for scenario in scenarios:
            document = self._read_json(scenario)
            self._validate_schema("conformance-scenario-v1.schema.json", document)
            if document.get("app_id") != manifest["id"]:
                raise VerificationError(f"scenario app_id mismatch: {scenario.name}")
            operations = [step.get("op") for step in document.get("steps", [])]
            if "clock.advance" not in operations or "assert.state" not in operations:
                raise VerificationError(
                    f"scenario lacks clock and state assertions: {scenario.name}"
                )
            self._run(
                [str(self.runtime_root / "doodad"), "conformance", str(scenario)]
            )
        gates.append("timer-conformance")

        preview = package / "preview.bmp"
        self._validate_preview(preview)
        gates.append("simulator-render")
        digest = self._tree_hash(package)
        artifact_id = f"{manifest['id']}@{manifest['version']}"
        summary = f"{manifest['name']} ({layout}) passed {len(gates)} independent gates."
        return VerifiedArtifact(
            artifact_id,
            str(package),
            str(preview),
            digest,
            summary,
            tuple(gates),
        )

    def _validate_schema(self, filename: str, document: object) -> None:
        path = self.runtime_root / "contracts" / filename
        schema = self._read_json(path)
        registry = Registry()
        for contract_path in (self.runtime_root / "contracts").glob("*.json"):
            contents = self._read_json(contract_path)
            try:
                resource = Resource.from_contents(contents)
            except Exception:
                continue
            registry = registry.with_resource(contract_path.as_uri(), resource)
            identifier = contents.get("$id")
            if isinstance(identifier, str):
                registry = registry.with_resource(identifier, resource)
        errors = sorted(
            Draft202012Validator(schema, registry=registry).iter_errors(document),
            key=lambda item: "/".join(map(str, item.path)),
        )
        if errors:
            raise VerificationError(
                f"{filename} validation failed: {errors[0].message}"
            )

    @staticmethod
    def _validate_identity(
        manifest: dict[str, Any], appspec: dict[str, Any], agent: dict[str, Any]
    ) -> None:
        app_id = manifest["id"]
        if not isinstance(appspec.get("app_id"), str) or not appspec["app_id"]:
            raise VerificationError("AppSpec app_id is missing")
        if agent.get("app_id") != app_id:
            raise VerificationError("agent contract app_id does not match manifest id")
        if agent.get("app_version") != manifest.get("version"):
            raise VerificationError("agent contract version does not match manifest")
        if agent.get("host_abi") != manifest.get("host_abi"):
            raise VerificationError("agent contract ABI does not match manifest")

    def _validate_permissions(
        self, manifest: dict[str, Any], agent: dict[str, Any]
    ) -> None:
        capabilities = set(manifest.get("capabilities", []))
        if "timer.schedule" not in capabilities or "ui.mount" not in capabilities:
            raise VerificationError("rest timer must request ui.mount and timer.schedule")
        unexpected = capabilities - self.ALLOWED_CAPABILITIES
        if unexpected:
            raise VerificationError(
                "rest timer requests forbidden capabilities: "
                + ", ".join(sorted(unexpected))
            )
        contract_permissions = {
            permission
            for entry in (*agent.get("views", []), *agent.get("actions", []))
            for permission in entry.get("permissions", [])
        }
        if not contract_permissions <= capabilities:
            raise VerificationError("agent contract references undeclared permissions")

    @staticmethod
    def _nodes(appspec: dict[str, Any]):  # type: ignore[no-untyped-def]
        stack = [appspec.get("screen")]
        while stack:
            node = stack.pop()
            if not isinstance(node, dict):
                continue
            yield node
            props = node.get("props", {})
            children = props.get("children", []) if isinstance(props, dict) else []
            if isinstance(children, list):
                stack.extend(reversed(children))

    def _validate_semantics(self, appspec: dict[str, Any], layout: str) -> None:
        nodes = list(self._nodes(appspec))
        progress = [node for node in nodes if node.get("type") == "progress"]
        if len(progress) != 1:
            raise VerificationError("rest timer must contain exactly one progress component")
        expected_style = "circular" if layout == "ring" else "linear"
        if progress[0].get("props", {}).get("style") != expected_style:
            raise VerificationError(f"{layout} layout requires {expected_style} progress")
        for node in nodes:
            if node.get("events"):
                semantics = node.get("semantics", {})
                label = semantics.get("label") if isinstance(semantics, dict) else None
                if not isinstance(label, str) or not label.strip():
                    raise VerificationError(
                        f"interactive node {node.get('id')} lacks a semantic label"
                    )
                if node.get("type") == "button":
                    size = node.get("props", {}).get("size", "default")
                    if size != "default":
                        raise VerificationError("timer action must use the 48dp default button")

    @staticmethod
    def _validate_timer_source(path: Path) -> None:
        try:
            source = path.read_text(encoding="utf-8")
        except OSError as error:
            raise VerificationError(f"cannot read generated Rust source: {error}") from error
        required = (
            "schedule_timer_after",
            "handle_provider_event",
            "decode_timer_provider_payload",
        )
        missing = [name for name in required if name not in source]
        if missing:
            raise VerificationError(
                "timer source does not use the exact scheduler contract: "
                + ", ".join(missing)
            )
        forbidden = ("std::time", "thread::sleep", "Instant::now", "SystemTime::now")
        used = [name for name in forbidden if name in source]
        if used:
            raise VerificationError(
                "guest-owned timing is forbidden: " + ", ".join(used)
            )

    def _compile_appspec(self, document: dict[str, Any], output: Path) -> None:
        tools = self.runtime_root / "tools"
        sys.path.insert(0, str(tools))
        try:
            from doodad_cli.appspec_cbor import compile_canonical_cbor

            output.write_bytes(compile_canonical_cbor(document))
        except Exception as error:
            raise VerificationError(f"AppSpec CBOR compilation failed: {error}") from error
        finally:
            try:
                sys.path.remove(str(tools))
            except ValueError:
                pass

    def _run(self, command: list[str], cwd: Path | None = None) -> None:
        environment = os.environ.copy()
        # The isolated workspace intentionally sits outside the repository,
        # so cwd-based asdf discovery cannot see the checked-in Rust pin.
        environment["ASDF_RUST_VERSION"] = "1.95.0"
        try:
            result = subprocess.run(
                command,
                cwd=cwd or self.runtime_root,
                env=environment,
                check=False,
                capture_output=True,
                text=True,
                timeout=self.timeout_seconds,
            )
        except subprocess.TimeoutExpired as error:
            raise VerificationError(
                f"{Path(command[0]).name} {command[1]} timed out"
            ) from error
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip().splitlines()
            tail = detail[-1][:300] if detail else "no diagnostic"
            raise VerificationError(
                f"{Path(command[0]).name} {command[1]} failed: {tail}"
            )

    @staticmethod
    def _validate_preview(path: Path) -> None:
        try:
            header = path.read_bytes()[:26]
        except OSError as error:
            raise VerificationError(f"simulator preview missing: {error}") from error
        if len(header) < 26 or header[:2] != b"BM":
            raise VerificationError("simulator preview is not a BMP")
        width, height = struct.unpack_from("<ii", header, 18)
        if (width, abs(height)) != (240, 240):
            raise VerificationError(
                f"simulator preview is {width}x{abs(height)}, expected 240x240"
            )

    @staticmethod
    def _tree_hash(directory: Path) -> str:
        digest = hashlib.sha256()
        for path in sorted(item for item in directory.rglob("*") if item.is_file()):
            relative = path.relative_to(directory).as_posix().encode("utf-8")
            digest.update(len(relative).to_bytes(4, "big"))
            digest.update(relative)
            payload = path.read_bytes()
            digest.update(len(payload).to_bytes(8, "big"))
            digest.update(payload)
        return digest.hexdigest()

    @staticmethod
    def _read_json(path: Path) -> dict[str, Any]:
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise VerificationError(f"cannot read {path}: {error}") from error
        if not isinstance(value, dict):
            raise VerificationError(f"{path} must contain a JSON object")
        return value
