"""Deterministic static JSON and HTML reports for Project Parallax."""

from __future__ import annotations

import html
import json
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence
from urllib.parse import quote, urlsplit

from .parallax_compare import (
    NodeEvidenceQualityAudit,
    PixelComparison,
    StructuredComparison,
)
from .parallax_contract import ID_PATTERN, SHA256_PATTERN


REQUIRED_IMAGE_ARTIFACTS = {
    "reference",
    "candidate",
    "side_by_side",
    "overlay",
    "difference",
}
REVIEW_STATES = {"pending", "approved", "rejected"}


@dataclass(frozen=True)
class ComparisonCaseReport:
    """One aligned render pair in a static report."""

    case_id: str
    app_slug: str
    title: str
    sequence: int
    snapshot_sha256: str
    profile_id: str
    capture_phase: str
    comparison_policy: str
    pixel: PixelComparison
    structured: StructuredComparison
    artifacts: Mapping[str, str]
    review_status: str = "pending"
    quality: Mapping[str, NodeEvidenceQualityAudit] = field(
        default_factory=dict
    )

    @property
    def exact(self) -> bool:
        return self.pixel.exact and self.structured.exact

    @property
    def quality_passed(self) -> bool:
        return all(audit.passed for audit in self.quality.values())

    def to_dict(self) -> dict[str, Any]:
        return {
            "case_id": self.case_id,
            "app_slug": self.app_slug,
            "title": self.title,
            "sequence": self.sequence,
            "snapshot_sha256": self.snapshot_sha256,
            "profile_id": self.profile_id,
            "capture_phase": self.capture_phase,
            "comparison_policy": self.comparison_policy,
            "result": "exact" if self.exact else "different",
            "review_status": self.review_status,
            "pixel": self.pixel.to_dict(),
            "structured": self.structured.to_dict(),
            "quality_passed": self.quality_passed,
            "quality": {
                name: audit.to_dict()
                for name, audit in sorted(self.quality.items())
            },
            "artifacts": dict(sorted(self.artifacts.items())),
        }


@dataclass(frozen=True)
class ParallaxReport:
    """A timestamp-free report manifest suitable for reproducible builds."""

    report_id: str
    title: str
    cases: Sequence[ComparisonCaseReport]
    schema_version: int = 1
    artifacts: Mapping[str, str] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        ordered = sorted(
            self.cases,
            key=lambda case: (
                case.app_slug,
                case.sequence,
                case.capture_phase,
                case.profile_id,
                case.case_id,
            ),
        )
        exact_count = sum(case.exact for case in ordered)
        quality_issue_count = sum(
            len(audit.issues)
            for case in ordered
            for audit in case.quality.values()
        )
        return {
            "schema_version": self.schema_version,
            "report_id": self.report_id,
            "title": self.title,
            "summary": {
                "case_count": len(ordered),
                "exact_count": exact_count,
                "different_count": len(ordered) - exact_count,
                "quality_passed_count": sum(
                    case.quality_passed for case in ordered
                ),
                "quality_issue_count": quality_issue_count,
            },
            "artifacts": dict(sorted(self.artifacts.items())),
            "cases": [case.to_dict() for case in ordered],
        }


@dataclass(frozen=True)
class StaticReportPaths:
    html: Path
    json: Path


def write_static_report(
    output_directory: str | Path,
    report: ParallaxReport,
    *,
    verify_links: bool = True,
) -> StaticReportPaths:
    """Write ``report.json`` and ``index.html`` with stable byte output."""

    root = Path(output_directory)
    _validate_report(report)
    if verify_links:
        _verify_artifacts(root, report.artifacts, report.cases)
    root.mkdir(parents=True, exist_ok=True)

    document = report.to_dict()
    json_bytes = (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")
    html_bytes = _render_html(document).encode("utf-8")
    json_path = root / "report.json"
    html_path = root / "index.html"
    json_path.write_bytes(json_bytes)
    html_path.write_bytes(html_bytes)
    return StaticReportPaths(html=html_path, json=json_path)


def validate_relative_link(value: str) -> str:
    """Validate an artifact link as a safe relative POSIX path."""

    if not isinstance(value, str) or not value:
        raise ValueError("artifact link must be a non-empty string")
    if "\\" in value:
        raise ValueError("artifact link must use POSIX separators")
    parsed = urlsplit(value)
    if (
        parsed.scheme
        or parsed.netloc
        or parsed.query
        or parsed.fragment
        or parsed.path != value
    ):
        raise ValueError("artifact link must be a plain relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise ValueError("artifact link must be a safe relative path")
    return value


def _validate_report(report: ParallaxReport) -> None:
    if report.schema_version != 1:
        raise ValueError("report schema_version must be 1")
    _identifier(report.report_id, "report_id")
    if not isinstance(report.title, str) or not report.title:
        raise ValueError("report title must be a non-empty string")
    if not report.cases:
        raise ValueError("report must contain at least one case")
    for name, link in report.artifacts.items():
        _identifier(name, "report artifact name")
        validate_relative_link(link)
    identities: set[str] = set()
    for case in report.cases:
        _identifier(case.case_id, "case_id")
        if case.case_id in identities:
            raise ValueError(f"duplicate case_id {case.case_id!r}")
        identities.add(case.case_id)
        _identifier(case.app_slug, "app_slug")
        _identifier(case.profile_id, "profile_id")
        _identifier(case.capture_phase, "capture_phase")
        _identifier(case.comparison_policy, "comparison_policy")
        if not isinstance(case.title, str) or not case.title:
            raise ValueError("case title must be a non-empty string")
        if (
            isinstance(case.sequence, bool)
            or not isinstance(case.sequence, int)
            or case.sequence < 0
        ):
            raise ValueError("case sequence must be a non-negative integer")
        if (
            not isinstance(case.snapshot_sha256, str)
            or SHA256_PATTERN.fullmatch(case.snapshot_sha256) is None
        ):
            raise ValueError("case snapshot_sha256 must be a SHA-256 digest")
        if case.review_status not in REVIEW_STATES:
            raise ValueError(
                f"review_status must be one of {sorted(REVIEW_STATES)}"
            )
        missing = REQUIRED_IMAGE_ARTIFACTS - set(case.artifacts)
        if missing:
            raise ValueError(
                f"case {case.case_id!r} is missing artifacts: {sorted(missing)}"
            )
        for name, link in case.artifacts.items():
            _identifier(name, "artifact name")
            validate_relative_link(link)


def _verify_artifacts(
    root: Path,
    report_artifacts: Mapping[str, str],
    cases: Sequence[ComparisonCaseReport],
) -> None:
    resolved_root = root.resolve()
    artifact_groups = [("report", report_artifacts)]
    artifact_groups.extend(
        (case.case_id, case.artifacts) for case in cases
    )
    for owner, artifacts in artifact_groups:
        for name, link in artifacts.items():
            safe_link = validate_relative_link(link)
            artifact = root.joinpath(*PurePosixPath(safe_link).parts)
            resolved_artifact = artifact.resolve()
            try:
                resolved_artifact.relative_to(resolved_root)
            except ValueError as error:
                raise ValueError(
                    f"artifact {owner}/{name!r} escapes the report directory"
                ) from error
            if not resolved_artifact.is_file():
                raise ValueError(
                    f"artifact {owner}/{name!r} does not exist: {safe_link}"
                )


def _identifier(value: str, name: str) -> None:
    if not isinstance(value, str) or ID_PATTERN.fullmatch(value) is None:
        raise ValueError(f"{name} must be a lowercase identifier")


def _render_html(document: dict[str, Any]) -> str:
    title = _escape(document["title"])
    summary = document["summary"]
    report_artifacts = document.get("artifacts", {})
    contact_sheet = report_artifacts.get("contact_sheet")
    report_images = (
        _render_image(
            contact_sheet,
            f"{document['title']} — all aligned render pairs",
            "All aligned render pairs: Wear Compose reference left, "
            "LVGL product renderer right",
        )
        if contact_sheet
        else ""
    )
    report_links = "".join(
        f'<li><a href="{_link(link)}">{_escape(name)}</a></li>'
        for name, link in sorted(report_artifacts.items())
        if name != "contact_sheet"
    )
    report_artifact_section = (
        f'<section class="summary"><div class="images">{report_images}</div>'
        + (f"<ul>{report_links}</ul>" if report_links else "")
        + "</section>"
        if report_images or report_links
        else ""
    )
    case_sections = "".join(
        _render_case(case) for case in document["cases"]
    )
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
:root {{ color-scheme: dark; font-family: system-ui, sans-serif; }}
body {{ background:#101114; color:#f3f3f3; margin:0; padding:24px; }}
main {{ margin:auto; max-width:1120px; }}
a {{ color:#9ecaff; }}
.summary,.case {{ background:#191b20; border:1px solid #343741;
  border-radius:16px; margin:16px 0; padding:16px; }}
.images {{ display:grid; gap:12px;
  grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); }}
figure {{ margin:0; }}
img {{ background:#000; border:1px solid #343741; border-radius:8px;
  display:block; height:auto; image-rendering:pixelated; max-width:100%; }}
figcaption {{ color:#b9bdc9; font-size:.85rem; margin-top:4px; }}
table {{ border-collapse:collapse; width:100%; }}
th,td {{ border-bottom:1px solid #343741; padding:8px; text-align:left;
  vertical-align:top; }}
code {{ overflow-wrap:anywhere; }}
.exact {{ color:#8ee69b; }} .different {{ color:#ffb4a8; }}
</style>
</head>
<body>
<main>
<h1>{title}</h1>
<p><a href="report.json">Machine-readable report</a></p>
<section class="summary">
<strong>{summary["case_count"]} cases</strong> ·
<span class="exact">{summary["exact_count"]} exact</span> ·
<span class="different">{summary["different_count"]} different</span> ·
{summary["quality_passed_count"]} pass renderer-local quality checks ·
{summary["quality_issue_count"]} quality issues
</section>
{report_artifact_section}
{case_sections}</main>
</body>
</html>
"""


def _render_case(case: dict[str, Any]) -> str:
    result = case["result"]
    title = _escape(case["title"])
    artifacts = case["artifacts"]
    image_order = (
        ("reference", "Wear Compose Material 3 reference"),
        ("candidate", "LVGL product renderer"),
        ("side_by_side", "Side by side"),
        ("overlay", "50/50 overlay"),
        ("difference", "Absolute difference"),
    )
    images = "".join(
        _render_image(
            artifacts[name],
            f"{case['title']} — {label}",
            label,
        )
        for name, label in image_order
    )
    other_links = "".join(
        f'<li><a href="{_link(link)}">{_escape(name)}</a></li>'
        for name, link in sorted(artifacts.items())
        if name not in REQUIRED_IMAGE_ARTIFACTS
    )
    links_section = (
        f"<h3>Evidence</h3><ul>{other_links}</ul>" if other_links else ""
    )
    pixel = case["pixel"]
    structured = case["structured"]
    quality = case.get("quality", {})
    quality_issue_count = sum(
        audit["issue_count"] for audit in quality.values()
    )
    quality_summary = (
        f"; renderer-local quality issues {quality_issue_count}"
        if quality
        else ""
    )
    mismatches = structured["mismatches"]
    mismatch_rows = "".join(_render_mismatch(row) for row in mismatches)
    mismatch_table = (
        """<h3>Structured mismatches</h3>
<table><thead><tr><th>Category</th><th>Source</th><th>Node / field</th>
<th>Expected</th><th>Actual</th></tr></thead>
<tbody>"""
        + mismatch_rows
        + "</tbody></table>"
        if mismatches
        else "<p class=\"exact\">Structured evidence is exact.</p>"
    )
    return f"""<section class="case" id="{_escape(case["case_id"])}">
<h2>{title} <span class="{result}">— {result}</span></h2>
<p><code>{_escape(case["app_slug"])}</code> · sequence {case["sequence"]} ·
<code>{_escape(case["profile_id"])}</code> ·
<code>{_escape(case["capture_phase"])}</code></p>
<p>Changed pixels: <strong>{pixel["changed_pixels"]}</strong> /
{pixel["pixel_count"]} ({pixel["changed_pixel_fraction"]:.6%}); MAE
{pixel["mae"]:.6f}; RMSE {pixel["rmse"]:.6f}; structured mismatches
{structured["mismatch_count"]}{quality_summary}.</p>
<div class="images">{images}</div>
{links_section}
{mismatch_table}
</section>
"""


def _render_image(link: str, alt: str, label: str) -> str:
    safe_link = _link(link)
    return (
        f'<figure><a href="{safe_link}"><img src="{safe_link}" '
        f'alt="{_escape(alt)}"></a><figcaption>{_escape(label)}</figcaption>'
        "</figure>"
    )


def _render_mismatch(mismatch: dict[str, Any]) -> str:
    node_field = (
        f"{mismatch['node_id'] or 'document'} / {mismatch['field']}"
    )
    expected = json.dumps(
        mismatch["expected"],
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    )
    actual = json.dumps(
        mismatch["actual"],
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    )
    return (
        "<tr>"
        f"<td>{_escape(mismatch['category'])}"
        f"<br><code>{_escape(mismatch['code'])}</code></td>"
        f"<td>{_escape(mismatch['source'])}</td>"
        f"<td><code>{_escape(node_field)}</code></td>"
        f"<td><code>{_escape(expected)}</code></td>"
        f"<td><code>{_escape(actual)}</code></td>"
        "</tr>"
    )


def _link(value: str) -> str:
    validate_relative_link(value)
    return html.escape(quote(value, safe="/._-"), quote=True)


def _escape(value: Any) -> str:
    return html.escape(str(value), quote=True)
