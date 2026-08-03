"""End-to-end Project Parallax capture and comparison orchestration."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

from .contract import DoodadError, find_project_root, read_json
from .parallax_compare import (
    audit_node_evidence_quality,
    compare_node_evidence,
    compare_reference_rgb888_to_candidate_rgb565le,
)
from .parallax_contract import (
    canonical_json_bytes,
    document_sha256,
    validate_node_evidence,
    validate_scene_snapshot,
)
from .parallax_image import (
    render_pair_contact_sheet,
    write_node_boundary_overlay_png,
    write_png_rgb565le,
    write_png_rgb888,
    write_render_pair_images,
)
from .parallax_report import (
    ComparisonCaseReport,
    ParallaxReport,
    StaticReportPaths,
    write_static_report,
)
from .perfect_render import (
    PerfectRenderSelection,
    capture_lvgl_suite,
    entry_output_directory,
    resolve_suite_entries,
    sha256_bytes,
)
from .rgb565 import (
    DEFAULT_HEIGHT,
    DEFAULT_WIDTH,
    rgb565le_to_rgb888,
    rgb888_to_rgb565le,
)


COMPOSE_CAPTURE_TEST = (
    "dev.doodad.reference.SceneSnapshotBatchCaptureTest"
)
COMPOSE_DENSITY = 1.25
COMPOSE_LOCALE = "en-US"
COMPOSE_TIME_ZONE = "UTC"


@dataclass(frozen=True)
class ComposeCaptureBatch:
    renderer_build_sha256: str
    manifests: tuple[dict[str, Any], ...]


@dataclass(frozen=True)
class PerfectRenderRun:
    output_root: Path
    report: StaticReportPaths
    contact_sheet: Path
    lvgl_manifests: tuple[dict[str, Any], ...]
    compose_manifests: tuple[dict[str, Any], ...]

    @property
    def case_count(self) -> int:
        return len(self.compose_manifests)

    @property
    def compose_build_sha256(self) -> str:
        if not self.compose_manifests:
            raise ValueError("perfect-render run has no Compose manifests")
        return self.compose_manifests[0]["renderer"]["build_sha256"]


CommandRunner = Callable[
    [Sequence[str], Path, dict[str, str]],
    subprocess.CompletedProcess[str],
]


def compose_renderer_build_sha256(project_root: Path) -> str:
    """Hash every source/config input that defines the host reference lane."""

    project_root = find_project_root(project_root)
    wear_root = project_root / "reference" / "android-wear"
    candidates = [
        wear_root / "app" / "build.gradle.kts",
        wear_root / "build.gradle.kts",
        wear_root / "gradle" / "libs.versions.toml",
        wear_root / "gradle.properties",
        wear_root / "settings.gradle.kts",
        project_root / "reference" / "interpretation-policy-v1.json",
    ]
    candidates.extend(
        sorted(
            (
                wear_root
                / "app"
                / "src"
                / "main"
                / "java"
                / "dev"
                / "doodad"
                / "reference"
            ).rglob("*.kt")
        )
    )
    candidates.append(
        wear_root
        / "app"
        / "src"
        / "test"
        / "java"
        / "dev"
        / "doodad"
        / "reference"
        / "SceneSnapshotBatchCaptureTest.kt"
    )
    missing = [path for path in candidates if not path.is_file()]
    if missing:
        raise DoodadError(
            "Compose renderer hash inputs are missing: "
            + ", ".join(str(path) for path in missing)
        )

    digest = hashlib.sha256()
    for path in sorted(set(candidates)):
        relative = path.relative_to(project_root).as_posix().encode("utf-8")
        payload = path.read_bytes()
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
    return digest.hexdigest()


def capture_compose_suite(
    project_root: Path,
    selections: Sequence[PerfectRenderSelection],
    output_root: Path,
    *,
    command_runner: CommandRunner | None = None,
) -> ComposeCaptureBatch:
    """Render selected shared snapshots through Wear Compose in one JVM."""

    project_root = find_project_root(project_root)
    if not selections:
        raise DoodadError("Compose capture requires at least one suite entry")
    supported_capture_phases = {
        "resting": "resting",
        "selected": "selected",
        "end_state": "end_state",
        "baseline_state": "resting",
        "extreme_state": "resting",
        "rain_state": "end_state",
        "stale_state": "disabled",
        "error_state": "error",
        "large_font": "resting",
    }
    for selection in selections:
        if (
            selection.entry["profile_id"] != "watch_square_240"
            or selection.entry["capture_phase"]
            not in supported_capture_phases
        ):
            raise DoodadError(
                "the host batch does not support the requested capture phase "
                "watch_square_240 captures only"
            )

    output_root = output_root.resolve()
    renderer_build_sha256 = compose_renderer_build_sha256(project_root)
    requests = []
    for selection in selections:
        output_directory = entry_output_directory(
            output_root, selection.entry
        )
        snapshot_path = output_directory / "scene-snapshot.json"
        if not snapshot_path.is_file():
            raise DoodadError(
                f"LVGL capture has not staged {snapshot_path}"
            )
        requests.append(
            {
                "snapshot": str(snapshot_path.resolve()),
                "output": str((output_directory / "compose.png").resolve()),
                "capture_phase": selection.entry["capture_phase"],
                "capture_state": supported_capture_phases[
                    selection.entry["capture_phase"]
                ],
                "font_scale_milli": selection.entry.get(
                    "font_scale_milli", 1000
                ),
            }
        )

    target_root = project_root / "target"
    target_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="parallax-compose-",
        dir=target_root,
    ) as temporary:
        manifest_path = Path(temporary) / "batch.json"
        manifest_path.write_bytes(canonical_json_bytes(requests))
        command = [
            "./gradlew",
            ":app:testDebugUnitTest",
            "--tests",
            COMPOSE_CAPTURE_TEST,
            "--rerun-tasks",
            "--console=plain",
            f"-Pparallax.manifest={manifest_path.resolve()}",
            (
                "-Pparallax.rendererBuildSha256="
                f"{renderer_build_sha256}"
            ),
        ]
        wear_root = project_root / "reference" / "android-wear"
        if command_runner is None:
            environment = _android_environment()
            completed = _run_command(command, wear_root, environment)
        else:
            environment = dict(os.environ)
            completed = command_runner(command, wear_root, environment)
        if completed.returncode != 0:
            diagnostic = "\n".join(
                (
                    completed.stdout or "",
                    completed.stderr or "",
                )
            ).strip()
            if len(diagnostic) > 12_000:
                diagnostic = diagnostic[-12_000:]
            raise DoodadError(
                "Wear Compose batch capture failed"
                + (f":\n{diagnostic}" if diagnostic else "")
            )

    manifests = tuple(
        _validate_compose_capture(
            selection,
            output_root,
            renderer_build_sha256,
        )
        for selection in selections
    )
    return ComposeCaptureBatch(
        renderer_build_sha256=renderer_build_sha256,
        manifests=manifests,
    )


def compare_captured_suite(
    project_root: Path,
    selections: Sequence[PerfectRenderSelection],
    output_root: Path,
) -> tuple[StaticReportPaths, Path]:
    """Build native-size artifacts, audits, contact sheet, JSON, and HTML."""

    project_root = find_project_root(project_root)
    if not selections:
        raise DoodadError("comparison requires at least one suite entry")
    output_root = output_root.resolve()
    report_root = output_root / "report"
    if report_root.is_dir():
        shutil.rmtree(report_root)
    cases: list[ComparisonCaseReport] = []
    contact_pairs: list[tuple[bytes, bytes]] = []
    titles = _app_titles(project_root)

    app_counts: dict[str, int] = {}
    for selection in selections:
        slug = selection.entry["app_slug"]
        app_counts[slug] = app_counts.get(slug, 0) + 1

    for selection in selections:
        entry = selection.entry
        capture_directory = entry_output_directory(output_root, entry)
        snapshot = read_json(capture_directory / "scene-snapshot.json")
        validate_scene_snapshot(snapshot)
        snapshot_sha256 = document_sha256(snapshot)
        if snapshot_sha256 != entry["snapshot_sha256"]:
            raise DoodadError(
                f"{entry['app_slug']} capture has a stale SceneSnapshot"
            )

        reference_rgb888 = (
            capture_directory / "compose.rgb888"
        ).read_bytes()
        candidate_rgb565 = (
            capture_directory / "lvgl.rgb565le"
        ).read_bytes()
        reference_nodes = read_json(
            capture_directory / "compose.node-evidence.json"
        )
        candidate_nodes = read_json(
            capture_directory / "lvgl-nodes.json"
        )
        validate_node_evidence(reference_nodes)
        validate_node_evidence(candidate_nodes)

        pixel = compare_reference_rgb888_to_candidate_rgb565le(
            reference_rgb888,
            candidate_rgb565,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        structured = compare_node_evidence(
            reference_nodes,
            candidate_nodes,
            snapshot=snapshot,
        )
        reference_quality = audit_node_evidence_quality(reference_nodes)
        candidate_quality = audit_node_evidence_quality(candidate_nodes)
        contact_pairs.append((reference_rgb888, candidate_rgb565))

        case_id = (
            f"{entry['app_slug']}.{entry['capture_phase']}."
            f"{entry['profile_id']}"
        )
        if app_counts[entry["app_slug"]] > 1:
            case_id += f".sequence-{int(entry['sequence']):04d}"
        case_name = entry["app_slug"]
        if app_counts[entry["app_slug"]] > 1:
            case_name = (
                f"{entry['app_slug']}-{entry['capture_phase']}-"
                f"{int(entry['sequence']):04d}"
            )
        case_directory = report_root / "cases" / case_name
        image_paths = write_render_pair_images(
            case_directory,
            reference_rgb888,
            candidate_rgb565,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        reference_rgb565 = rgb888_to_rgb565le(
            reference_rgb888,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        write_png_rgb565le(
            case_directory / "reference_rgb565.png",
            reference_rgb565,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        write_node_boundary_overlay_png(
            case_directory / "reference_boundaries.png",
            reference_rgb888,
            reference_nodes,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )
        write_node_boundary_overlay_png(
            case_directory / "candidate_boundaries.png",
            rgb565le_to_rgb888(
                candidate_rgb565,
                width=DEFAULT_WIDTH,
                height=DEFAULT_HEIGHT,
            ),
            candidate_nodes,
            width=DEFAULT_WIDTH,
            height=DEFAULT_HEIGHT,
        )

        evidence_sources = {
            "scene_snapshot": capture_directory / "scene-snapshot.json",
            "reference_rgb888": capture_directory / "compose.rgb888",
            "reference_rgb888_metadata":
                capture_directory / "compose.rgb888.json",
            "candidate_rgb565le": capture_directory / "lvgl.rgb565le",
            "reference_evidence":
                capture_directory / "compose.node-evidence.json",
            "candidate_evidence": capture_directory / "lvgl-nodes.json",
            "reference_manifest":
                capture_directory / "compose-manifest.json",
            "candidate_manifest": capture_directory / "manifest.json",
        }
        artifact_paths = {
            name: _copy_artifact(source, case_directory, name)
            for name, source in evidence_sources.items()
        }
        _atomic_write(
            case_directory / "reference.rgb565le",
            reference_rgb565,
        )
        artifact_paths["reference_rgb565le"] = (
            case_directory / "reference.rgb565le"
        )
        artifact_paths.update(
            {
                "reference_rgb565_image":
                    case_directory / "reference_rgb565.png",
                "reference_boundaries":
                    case_directory / "reference_boundaries.png",
                "candidate_boundaries":
                    case_directory / "candidate_boundaries.png",
            }
        )

        metrics = {
            "schema_version": 1,
            "case_id": case_id,
            "snapshot_sha256": snapshot_sha256,
            "pixel": pixel.to_dict(),
            "structured": structured.to_dict(),
            "quality": {
                "wear_compose_reference": reference_quality.to_dict(),
                "lvgl_product": candidate_quality.to_dict(),
            },
        }
        _atomic_write(
            case_directory / "metrics.json",
            canonical_json_bytes(metrics),
        )
        artifact_paths["metrics"] = case_directory / "metrics.json"
        review = {
            "schema_version": 1,
            "status": entry["review"]["status"],
            "fidelity": "planned",
            "notes": [],
        }
        _atomic_write(
            case_directory / "review.json",
            canonical_json_bytes(review),
        )
        artifact_paths["review"] = case_directory / "review.json"

        artifacts = {
            name: path.relative_to(report_root).as_posix()
            for name, path in artifact_paths.items()
        }
        artifacts.update(
            {
                name: path.relative_to(report_root).as_posix()
                for name, path in image_paths.as_mapping().items()
            }
        )
        cases.append(
            ComparisonCaseReport(
                case_id=case_id,
                app_slug=entry["app_slug"],
                title=titles.get(entry["app_slug"], entry["app_slug"]),
                sequence=int(entry["sequence"]),
                snapshot_sha256=snapshot_sha256,
                profile_id=entry["profile_id"],
                capture_phase=entry["capture_phase"],
                comparison_policy=entry["comparison_policy"],
                pixel=pixel,
                structured=structured,
                artifacts=artifacts,
                review_status=entry["review"]["status"],
                quality={
                    "wear_compose_reference": reference_quality,
                    "lvgl_product": candidate_quality,
                },
            )
        )

    sheet = render_pair_contact_sheet(
        contact_pairs,
        width=DEFAULT_WIDTH,
        height=DEFAULT_HEIGHT,
        columns=5,
    )
    contact_sheet = write_png_rgb888(
        report_root / "contact-sheet.png",
        sheet.pixels,
        width=sheet.width,
        height=sheet.height,
    )
    report = ParallaxReport(
        report_id="parallax.perfect-render-20",
        title=(
            "Project Parallax — Wear Compose Material 3 reference "
            "versus LVGL"
        ),
        cases=cases,
        artifacts={"contact_sheet": "contact-sheet.png"},
    )
    return write_static_report(report_root, report), contact_sheet


def run_perfect_render_suite(
    project_root: Path,
    suite_path: Path,
    output_root: Path,
    *,
    app_slug: str | None = None,
    command_runner: CommandRunner | None = None,
) -> PerfectRenderRun:
    """Run the complete execute-once/render-twice host comparison."""

    project_root = find_project_root(project_root)
    selections = resolve_suite_entries(suite_path, app_slug=app_slug)
    lvgl_manifests = tuple(
        capture_lvgl_suite(
            project_root,
            suite_path,
            output_root,
            app_slug=app_slug,
        )
    )
    compose = capture_compose_suite(
        project_root,
        selections,
        output_root,
        command_runner=command_runner,
    )
    report, contact_sheet = compare_captured_suite(
        project_root,
        selections,
        output_root,
    )
    return PerfectRenderRun(
        output_root=output_root.resolve(),
        report=report,
        contact_sheet=contact_sheet,
        lvgl_manifests=lvgl_manifests,
        compose_manifests=compose.manifests,
    )


def _validate_compose_capture(
    selection: PerfectRenderSelection,
    output_root: Path,
    renderer_build_sha256: str,
) -> dict[str, Any]:
    entry = selection.entry
    output_directory = entry_output_directory(output_root, entry)
    paths = {
        "png": output_directory / "compose.png",
        "rgb888": output_directory / "compose.rgb888",
        "rgb888_metadata": output_directory / "compose.rgb888.json",
        "node_evidence": output_directory / "compose.node-evidence.json",
    }
    missing = [name for name, path in paths.items() if not path.is_file()]
    if missing:
        raise DoodadError(
            f"{entry['app_slug']} Compose capture is missing {missing}"
        )
    png = paths["png"].read_bytes()
    if not png.startswith(b"\x89PNG\r\n\x1a\n"):
        raise DoodadError(
            f"{entry['app_slug']} Compose capture is not a PNG"
        )
    rgb888 = paths["rgb888"].read_bytes()
    expected_bytes = DEFAULT_WIDTH * DEFAULT_HEIGHT * 3
    if len(rgb888) != expected_bytes:
        raise DoodadError(
            f"{entry['app_slug']} Compose RGB888 has {len(rgb888)} bytes; "
            f"expected {expected_bytes}"
        )
    metadata = read_json(paths["rgb888_metadata"])
    expected_metadata = {
        "schema_version": 1,
        "width": DEFAULT_WIDTH,
        "height": DEFAULT_HEIGHT,
        "stride_bytes": DEFAULT_WIDTH * 3,
        "pixel_format": "rgb888",
        "byte_order": "r_g_b",
        "bytes": expected_bytes,
    }
    if metadata != expected_metadata:
        raise DoodadError(
            f"{entry['app_slug']} Compose RGB888 metadata is invalid"
        )
    evidence = read_json(paths["node_evidence"])
    validate_node_evidence(evidence)
    expected_renderer = entry["compose"]
    renderer = evidence["renderer"]
    if (
        renderer["kind"] != "compose"
        or renderer["mode"] != expected_renderer["mode"]
        or renderer["version"] != expected_renderer["version"]
        or renderer["build_sha256"] != renderer_build_sha256
    ):
        raise DoodadError(
            f"{entry['app_slug']} Compose renderer attestation is stale"
        )
    if (
        evidence["snapshot_sha256"] != entry["snapshot_sha256"]
        or evidence["profile_id"] != entry["profile_id"]
        or evidence["capture_phase"]["id"] != entry["capture_phase"]
        or evidence["physical_width_px"] != DEFAULT_WIDTH
        or evidence["physical_height_px"] != DEFAULT_HEIGHT
    ):
        raise DoodadError(
            f"{entry['app_slug']} Compose evidence does not match the suite"
        )
    if [node["id"] for node in evidence["nodes"]] != [
        node["id"] for node in selection.snapshot["nodes"]
    ]:
        raise DoodadError(
            f"{entry['app_slug']} Compose evidence is missing scene nodes"
        )

    manifest = {
        "schema_version": 1,
        "kind": "parallax-compose-capture",
        "suite": {
            "id": selection.suite_id,
            "sha256": selection.suite_sha256,
        },
        "selection": {
            "app_slug": entry["app_slug"],
            "sequence": entry["sequence"],
            "scene_revision": selection.target_entry["scene_revision"],
            "capture_phase": entry["capture_phase"],
            "profile_id": entry["profile_id"],
            "snapshot_sha256": entry["snapshot_sha256"],
        },
        "renderer": renderer,
        "framebuffer": {
            "format": "rgb888",
            "physical_width_px": DEFAULT_WIDTH,
            "physical_height_px": DEFAULT_HEIGHT,
            "logical_width_dp": 192,
            "logical_height_dp": 192,
            "density": COMPOSE_DENSITY,
            "font_scale": entry.get("font_scale_milli", 1000) / 1000,
            "row_order": "top_to_bottom",
        },
        "environment": {
            "locale": COMPOSE_LOCALE,
            "time_zone": COMPOSE_TIME_ZONE,
            "dynamic_color": False,
            "reduced_motion": True,
        },
        "hashes": {
            "snapshot_sha256": entry["snapshot_sha256"],
            "renderer_source_sha256": renderer_build_sha256,
            "interpretation_policy_sha256": document_sha256(
                read_json(
                    selection.suite_path.parent
                    / "interpretation-policy-v1.json"
                )
            ),
            "png_sha256": sha256_bytes(png),
            "rgb888_sha256": sha256_bytes(rgb888),
            "node_evidence_sha256": document_sha256(evidence),
        },
        "artifacts": {
            name: {
                "path": path.name,
                "sha256": sha256_bytes(path.read_bytes()),
                "bytes": path.stat().st_size,
            }
            for name, path in paths.items()
        },
        "attestations": {
            "suite_snapshot_hash_shared": True,
            "renderer_snapshot_hash_shared": True,
            "native_size": True,
        },
    }
    _atomic_write(
        output_directory / "compose-manifest.json",
        canonical_json_bytes(manifest),
    )
    return manifest


def _app_titles(project_root: Path) -> dict[str, str]:
    catalog = read_json(project_root / "apps" / "conformance-suite.json")
    return {
        entry["slug"]: entry["name"]
        for entry in catalog["apps"]
    }


def _copy_artifact(
    source: Path,
    destination_directory: Path,
    stem: str,
) -> Path:
    if not source.is_file():
        raise DoodadError(f"missing comparison artifact: {source}")
    suffixes = "".join(source.suffixes)
    destination = destination_directory / f"{stem}{suffixes}"
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    shutil.copyfile(source, temporary)
    temporary.replace(destination)
    return destination


def _atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)


def _android_environment() -> dict[str, str]:
    environment = dict(os.environ)
    android_home = environment.get("ANDROID_HOME")
    if not android_home:
        android_home = str(Path.home() / "Library" / "Android" / "sdk")
        environment["ANDROID_HOME"] = android_home
    java_home = environment.get("JAVA_HOME")
    if not java_home:
        bundled = Path(
            "/Applications/Android Studio.app/Contents/jbr/Contents/Home"
        )
        if bundled.is_dir():
            java_home = str(bundled)
            environment["JAVA_HOME"] = java_home
    if not java_home or not (Path(java_home) / "bin" / "java").is_file():
        raise DoodadError(
            "a JDK 17+ is required; Android Studio's bundled JDK is supported"
        )
    if not Path(android_home).is_dir():
        raise DoodadError(f"Android SDK not found at {android_home}")
    environment["LC_ALL"] = "en_US.UTF-8"
    environment["TZ"] = COMPOSE_TIME_ZONE
    return environment


def _run_command(
    command: Sequence[str],
    cwd: Path,
    environment: dict[str, str],
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        cwd=cwd,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
