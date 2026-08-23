"""Independent deterministic gates for bounded generated applications."""

from __future__ import annotations

import hashlib
import hmac
import json
import math
import os
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from PIL import Image, ImageChops, ImageFilter, ImageStat
from referencing import Registry, Resource


class VerificationError(RuntimeError):
    pass


def package_tree_snapshot(directory: Path) -> tuple[str, dict[str, bytes]]:
    """Read and hash one exact package snapshot.

    The returned bytes are the bytes covered by the digest.  The outer
    packager uses this boundary so it cannot verify one mutable package tree
    and then accidentally sign a later version of ``manifest.json`` or
    ``app.wasm``.
    """

    files: dict[str, bytes] = {}
    for path in sorted(item for item in directory.rglob("*") if item.is_file()):
        relative = path.relative_to(directory).as_posix()
        files[relative] = path.read_bytes()
    digest = hashlib.sha256()
    for relative, payload in files.items():
        encoded_relative = relative.encode("utf-8")
        digest.update(len(encoded_relative).to_bytes(4, "big"))
        digest.update(encoded_relative)
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest(), files


@dataclass(frozen=True)
class VerifiedArtifact:
    artifact_id: str
    package_path: str
    preview_path: str
    sha256: str
    summary: str
    gates: tuple[str, ...]
    visual_review: dict[str, Any] | None = None

    def document(self) -> dict[str, Any]:
        document = {
            "artifact_id": self.artifact_id,
            "package_path": self.package_path,
            "preview_path": self.preview_path,
            "sha256": self.sha256,
            "summary": self.summary,
            "gates": list(self.gates),
        }
        if self.visual_review is not None:
            document["visual_review"] = self.visual_review
        return document


class GeneratedAppVerifier:
    DEFAULT_CAPABILITIES = {
        "ui.mount",
        "timer.schedule",
        "timer.cancel",
        "timer.acknowledge",
    }

    def __init__(self, runtime_root: Path, timeout_seconds: int = 600) -> None:
        self.runtime_root = runtime_root.resolve()
        self.timeout_seconds = timeout_seconds
        try:
            configured_visual_rmse = float(
                os.getenv("DOODAD_VISUAL_MAX_RMSE", "0.38")
            )
        except ValueError as error:
            raise ValueError("DOODAD_VISUAL_MAX_RMSE must be numeric") from error
        if not 0.05 <= configured_visual_rmse <= 0.75:
            raise ValueError("DOODAD_VISUAL_MAX_RMSE must be between 0.05 and 0.75")
        self.visual_max_rmse = configured_visual_rmse
        configured = os.getenv("DOODAD_GENERATED_CAPABILITIES", "")
        self.allowed_capabilities = (
            {item.strip() for item in configured.split(",") if item.strip()}
            if configured.strip()
            else set(self.DEFAULT_CAPABILITIES)
        )
        self.allowed_capabilities.add("ui.mount")
        abi = self._read_json(self.runtime_root / "contracts" / "abi" / "v1.json")
        imports = abi.get("capabilities", {})
        unknown = self.allowed_capabilities - (
            set(imports) if isinstance(imports, dict) else set()
        )
        if unknown:
            raise ValueError(
                "configured generated capabilities are absent from ABI v1: "
                + ", ".join(sorted(unknown))
            )

    def verify(self, workspace: Path, plan: dict[str, Any] | None = None) -> VerifiedArtifact:
        app = workspace / "app"
        try:
            expected_reference = (workspace / "REFERENCE_SHA256").read_text(
                encoding="ascii"
            ).strip()
            actual_reference, _ = package_tree_snapshot(workspace / "reference")
        except OSError as error:
            raise VerificationError(f"cannot audit generated reference tree: {error}") from error
        if not hmac.compare_digest(expected_reference, actual_reference):
            raise VerificationError("generated app modified its immutable reference tree")
        plan = plan or self._read_json(workspace / "BUILD_PLAN.json")
        manifest = self._read_json(app / "manifest.json")
        appspec = self._read_json(app / "appspec.json")
        agent = self._read_json(app / "agent.json")
        self._validate_schema("generated-app-plan-v1.schema.json", plan)
        design, primary_target = self.validate_design(workspace, plan)
        self._validate_schema("manifest-v1.schema.json", manifest)
        self._validate_schema("appspec-v1.schema.json", appspec)
        self._validate_schema("agent-contract-v1.schema.json", agent)
        self._validate_identity(manifest, appspec, agent)
        self._validate_plan(manifest, plan)
        self._validate_permissions(manifest, agent)
        self._validate_semantics(appspec)
        if "timer.schedule" in manifest.get("capabilities", []):
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
        gates = ["schema", "plan", "semantics", "permissions"]
        if "timer.schedule" in manifest.get("capabilities", []):
            gates.append("timer-source")
        for command in ("build", "check", "test"):
            self._run([str(self.runtime_root / "doodad"), command, str(app)])
            gates.append(command)

        package = app / "target" / "doodad" / str(manifest["id"])
        wasm = package / "app.wasm"
        self._run([str(self.runtime_root / "doodad"), "inspect", str(wasm)])
        gates.append("wasm-inspect")

        scenarios = sorted((app / "scenarios").glob("*.scenario.json"))
        if not scenarios:
            raise VerificationError("generated app has no deterministic scenario")
        observed_ops: set[str] = set()
        observed_scenarios: dict[str, set[str]] = {}
        for scenario in scenarios:
            document = self._read_json(scenario)
            self._validate_schema("conformance-scenario-v1.schema.json", document)
            if document.get("app_id") != manifest["id"]:
                raise VerificationError(f"scenario app_id mismatch: {scenario.name}")
            operations = [step.get("op") for step in document.get("steps", [])]
            observed_ops.update(str(operation) for operation in operations)
            observed_scenarios[str(document.get("id", ""))] = {
                str(operation) for operation in operations
            }
            self._run(
                [str(self.runtime_root / "doodad"), "conformance", str(scenario)]
            )
        for expected in plan.get("scenarios", []):
            scenario_id = str(expected.get("id", ""))
            actual = observed_scenarios.get(scenario_id)
            if actual is None:
                raise VerificationError(f"build-plan scenario is missing: {scenario_id}")
            missing = set(expected.get("required_ops", [])) - actual
            if missing:
                raise VerificationError(
                    f"scenario {scenario_id} lacks planned operations: "
                    + ", ".join(sorted(missing))
                )
        required_ops = {"action.dispatch", "assert.state"}
        capabilities = set(manifest.get("capabilities", []))
        if "timer.schedule" in capabilities:
            required_ops.add("clock.advance")
        if capabilities - {"ui.mount", "timer.schedule", "timer.cancel", "timer.acknowledge"}:
            required_ops.update({"provider.emit", "lifecycle.set"})
        missing_ops = sorted(required_ops - observed_ops)
        if missing_ops:
            raise VerificationError(
                "generated scenarios lack required operations: " + ", ".join(missing_ops)
            )
        gates.append("conformance")

        preview = package / "preview.bmp"
        self._validate_preview(preview)
        gates.append("simulator-render")
        visual_review = self._compare_visual_target(
            workspace, preview, primary_target, design
        )
        gates.append("visual-target-compare")
        digest, _ = package_tree_snapshot(package)
        artifact_id = f"{manifest['id']}@{manifest['version']}"
        summary = f"{manifest['name']} passed {len(gates)} independent gates."
        return VerifiedArtifact(
            artifact_id,
            str(package),
            str(preview),
            digest,
            summary,
            tuple(gates),
            visual_review,
        )

    def validate_plan(self, plan: dict[str, Any]) -> None:
        """Validate a plan before asking the user to approve it."""

        self._validate_schema("generated-app-plan-v1.schema.json", plan)

    def validate_design(
        self, workspace: Path, plan: dict[str, Any]
    ) -> tuple[dict[str, Any], Path]:
        """Validate immutable ImageGen targets before implementation begins."""

        approval = self._read_json(workspace / "PLAN_APPROVAL.json")
        approved_sha256 = approval.get("plan_sha256")
        if approved_sha256 != self.plan_sha256(plan):
            raise VerificationError("build plan no longer matches its voice approval")
        design = self._read_json(workspace / "design" / "DESIGN_MANIFEST.json")
        self._validate_schema("generated-app-design-v1.schema.json", design)
        if design.get("app_id") != plan.get("app_id"):
            raise VerificationError("design app_id does not match the approved plan")
        if design.get("plan_sha256") != self.plan_sha256(plan):
            raise VerificationError("design does not target the approved plan revision")
        references = design.get("source_references", [])
        for relative in references:
            path = (workspace / str(relative)).resolve()
            reference_root = (workspace / "reference" / "design-language").resolve()
            if not path.is_relative_to(reference_root) or not path.is_file():
                raise VerificationError(
                    f"design source reference is unavailable: {relative}"
                )
        primary = [
            screen for screen in design.get("screens", []) if screen.get("primary") is True
        ]
        if len(primary) != 1:
            raise VerificationError("design manifest must select exactly one primary screen")
        for screen in design.get("screens", []):
            target = (workspace / "design" / str(screen["target"])).resolve()
            target_root = (workspace / "design" / "targets").resolve()
            if not target.is_relative_to(target_root) or not target.is_file():
                raise VerificationError(f"design target is unavailable: {screen['target']}")
            self._validate_target_png(target)
        return design, (workspace / "design" / str(primary[0]["target"])).resolve()

    @staticmethod
    def plan_sha256(plan: dict[str, Any]) -> str:
        payload = json.dumps(
            plan, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
        return hashlib.sha256(payload).hexdigest()

    @staticmethod
    def _validate_target_png(path: Path) -> None:
        try:
            with Image.open(path) as image:
                if image.format != "PNG":
                    raise VerificationError(f"design target is not a PNG: {path.name}")
                if image.size != (240, 240):
                    raise VerificationError(
                        f"design target is {image.width}x{image.height}, expected 240x240"
                    )
        except OSError as error:
            raise VerificationError(f"cannot read design target: {error}") from error

    def _compare_visual_target(
        self,
        workspace: Path,
        preview: Path,
        target: Path,
        design: dict[str, Any],
    ) -> dict[str, Any]:
        try:
            with Image.open(target) as opened_target, Image.open(preview) as opened_preview:
                target_rgb = opened_target.convert("RGB")
                preview_rgb = opened_preview.convert("RGB")
        except OSError as error:
            raise VerificationError(f"cannot compare simulator visual: {error}") from error
        if preview_rgb.size != target_rgb.size:
            raise VerificationError("simulator and design target dimensions differ")

        target_structure = target_rgb.filter(ImageFilter.GaussianBlur(2)).resize(
            (48, 48), Image.Resampling.LANCZOS
        )
        preview_structure = preview_rgb.filter(ImageFilter.GaussianBlur(2)).resize(
            (48, 48), Image.Resampling.LANCZOS
        )
        structural_rmse = self._normalized_rmse(target_structure, preview_structure)
        pixel_rmse = self._normalized_rmse(target_rgb, preview_rgb)
        target_mean = ImageStat.Stat(target_rgb).mean
        preview_mean = ImageStat.Stat(preview_rgb).mean
        theme_distance = math.sqrt(
            sum((left - right) ** 2 for left, right in zip(target_mean, preview_mean))
            / 3
        ) / 255

        review_root = workspace / "design" / "review"
        review_root.mkdir(parents=True, exist_ok=True)
        side_by_side = Image.new("RGB", (480, 240), "black")
        side_by_side.paste(target_rgb, (0, 0))
        side_by_side.paste(preview_rgb, (240, 0))
        side_by_side_path = review_root / "target-vs-simulator.png"
        side_by_side.save(side_by_side_path)
        difference_path = review_root / "difference.png"
        ImageChops.difference(target_rgb, preview_rgb).save(difference_path)
        report = {
            "schema_version": 1,
            "primary_screen": next(
                screen["id"] for screen in design["screens"] if screen["primary"]
            ),
            "target_path": str(target),
            "simulator_path": str(preview),
            "structural_rmse": round(structural_rmse, 6),
            "pixel_rmse": round(pixel_rmse, 6),
            "theme_distance": round(theme_distance, 6),
            "maximum_structural_rmse": self.visual_max_rmse,
            "maximum_theme_distance": 0.25,
            "side_by_side_path": str(side_by_side_path),
            "difference_path": str(difference_path),
        }
        report_path = review_root / "VISUAL_COMPARISON.json"
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        report["report_path"] = str(report_path)
        if structural_rmse > self.visual_max_rmse or theme_distance > 0.25:
            raise VerificationError(
                "simulator visual missed the approved target "
                f"(structural RMSE {structural_rmse:.3f}/{self.visual_max_rmse:.3f}, "
                f"theme distance {theme_distance:.3f}/0.250); "
                "inspect design/review/target-vs-simulator.png"
            )
        return report

    @staticmethod
    def _normalized_rmse(left: Image.Image, right: Image.Image) -> float:
        histogram = ImageChops.difference(left, right).histogram()
        squared = sum((index % 256) ** 2 * count for index, count in enumerate(histogram))
        samples = left.width * left.height * len(left.getbands())
        return math.sqrt(squared / samples) / 255

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
        # AppSpec app_id is the guest UI/event namespace (for example
        # "timer"), not the signed reverse-domain package identity. Runtime
        # ownership is bound separately to the resident package generation.
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
        if "ui.mount" not in capabilities:
            raise VerificationError("generated apps must request ui.mount")
        timer_dependents = {"timer.cancel", "timer.acknowledge"} & capabilities
        if timer_dependents and "timer.schedule" not in capabilities:
            raise VerificationError("timer cancel/acknowledge requires timer.schedule")
        unexpected = capabilities - self.allowed_capabilities
        if unexpected:
            raise VerificationError(
                "generated app requests unavailable capabilities: "
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

    @staticmethod
    def _validate_plan(manifest: dict[str, Any], plan: dict[str, Any]) -> None:
        comparisons = {
            "id": "app_id",
            "name": "name",
            "version": "version",
            "identity": "identity",
            "capabilities": "capabilities",
        }
        for manifest_key, plan_key in comparisons.items():
            if manifest.get(manifest_key) != plan.get(plan_key):
                raise VerificationError(
                    f"manifest {manifest_key} does not match generated build plan"
                )

    def _validate_semantics(self, appspec: dict[str, Any]) -> None:
        nodes = list(self._nodes(appspec))
        if not nodes:
            raise VerificationError("generated AppSpec has no nodes")
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
                        raise VerificationError("generated buttons must use the 48dp default size")

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
        # Generated build inputs run before the trusted outer packager and must
        # never inherit the user's personal signing material.
        environment.pop("DOODAD_PERSONAL_HMAC_KEY_HEX", None)
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
        digest, _ = package_tree_snapshot(directory)
        return digest

    @staticmethod
    def _read_json(path: Path) -> dict[str, Any]:
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise VerificationError(f"cannot read {path}: {error}") from error
        if not isinstance(value, dict):
            raise VerificationError(f"{path} must contain a JSON object")
        return value


# Transitional import name for callers that have not yet moved to the generic
# verifier. Its behavior is intentionally generic.
RestTimerVerifier = GeneratedAppVerifier
