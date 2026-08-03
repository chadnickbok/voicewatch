"""Structured and pixel comparison primitives for Project Parallax."""

from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass
from typing import Any, Iterable

from .parallax_contract import (
    canonical_json_bytes,
    document_sha256,
    validate_node_evidence,
    validate_scene_snapshot,
)
from .rgb565 import (
    RGB565Comparison,
    compare_rgb565le,
    rgb888_to_rgb565le,
)


MISMATCH_TAXONOMY = {
    "snapshot_digest": "metadata",
    "snapshot_identity": "metadata",
    "capture_phase": "metadata",
    "profile": "metadata",
    "dimensions": "metadata",
    "node_missing": "node_presence",
    "node_unexpected": "node_presence",
    "snapshot_node_missing": "node_presence",
    "snapshot_node_unexpected": "node_presence",
    "parent": "hierarchy",
    "role": "semantics",
    "label": "semantics",
    "value": "semantics",
    "state_description": "semantics",
    "visible": "state",
    "enabled": "state",
    "selected": "state",
    "checked": "state",
    "actions": "actions",
    "bounds_px": "bounds",
    "bounds_dp_q8_8": "bounds",
    "token_missing": "tokens",
    "token_unexpected": "tokens",
    "token_value": "tokens",
    "text_presence": "text_layout",
    "line_count": "text_layout",
    "truncated": "text_layout",
    "baselines_px": "text_layout",
}

QUALITY_ISSUE_TAXONOMY = {
    "visible_bounds_nonpositive": "geometry",
    "visible_bounds_out_of_viewport": "geometry",
    "interactive_target_too_small": "touch_target",
}


@dataclass(frozen=True)
class PixelComparison:
    """JSON-friendly exact totals derived from the canonical RGB565 metric."""

    width: int
    height: int
    pixel_count: int
    changed_pixels: int
    absolute_error_sum: int
    squared_error_sum: int
    max_channel_error: int

    @classmethod
    def from_rgb565(cls, comparison: RGB565Comparison) -> "PixelComparison":
        return cls(
            width=comparison.width,
            height=comparison.height,
            pixel_count=comparison.pixel_count,
            changed_pixels=comparison.changed_pixels,
            absolute_error_sum=comparison.absolute_error_sum,
            squared_error_sum=comparison.squared_error_sum,
            max_channel_error=comparison.max_channel_error,
        )

    @property
    def exact(self) -> bool:
        return self.changed_pixels == 0

    @property
    def channel_samples(self) -> int:
        return self.pixel_count * 3

    @property
    def changed_pixel_fraction(self) -> float:
        return self.changed_pixels / self.pixel_count

    @property
    def mae(self) -> float:
        return self.absolute_error_sum / self.channel_samples

    @property
    def mse(self) -> float:
        return self.squared_error_sum / self.channel_samples

    @property
    def rmse(self) -> float:
        return math.sqrt(self.mse)

    def to_dict(self) -> dict[str, int | float | bool]:
        return {
            "width": self.width,
            "height": self.height,
            "pixel_count": self.pixel_count,
            "channel_samples": self.channel_samples,
            "changed_pixels": self.changed_pixels,
            "changed_pixel_fraction": self.changed_pixel_fraction,
            "absolute_error_sum": self.absolute_error_sum,
            "squared_error_sum": self.squared_error_sum,
            "max_channel_error": self.max_channel_error,
            "mae": self.mae,
            "mse": self.mse,
            "rmse": self.rmse,
            "exact": self.exact,
        }


@dataclass(frozen=True)
class Mismatch:
    """One normalized structured mismatch."""

    category: str
    code: str
    source: str
    node_id: str | None
    field: str
    expected: Any
    actual: Any

    def to_dict(self) -> dict[str, Any]:
        return {
            "category": self.category,
            "code": self.code,
            "source": self.source,
            "node_id": self.node_id,
            "field": self.field,
            "expected": self.expected,
            "actual": self.actual,
        }


@dataclass(frozen=True)
class StructuredComparison:
    """A stable, renderer-neutral collection of structured mismatches."""

    reference_node_count: int
    candidate_node_count: int
    compared_node_count: int
    mismatches: tuple[Mismatch, ...]

    @property
    def exact(self) -> bool:
        return not self.mismatches

    @property
    def mismatch_counts(self) -> dict[str, int]:
        counts = Counter(mismatch.category for mismatch in self.mismatches)
        return dict(sorted(counts.items()))

    def to_dict(self) -> dict[str, Any]:
        return {
            "reference_node_count": self.reference_node_count,
            "candidate_node_count": self.candidate_node_count,
            "compared_node_count": self.compared_node_count,
            "mismatch_count": len(self.mismatches),
            "mismatch_counts": self.mismatch_counts,
            "exact": self.exact,
            "mismatches": [
                mismatch.to_dict() for mismatch in self.mismatches
            ],
        }


@dataclass(frozen=True)
class QualityIssue:
    """One renderer-local NodeEvidence quality issue."""

    category: str
    code: str
    node_id: str
    field: str
    expected: Any
    actual: Any

    def to_dict(self) -> dict[str, Any]:
        return {
            "category": self.category,
            "code": self.code,
            "node_id": self.node_id,
            "field": self.field,
            "expected": self.expected,
            "actual": self.actual,
        }


@dataclass(frozen=True)
class NodeEvidenceQualityAudit:
    """Stable renderer-local geometry and touch-target audit results."""

    node_count: int
    visible_node_count: int
    interactive_node_count: int
    minimum_touch_target_dp: int
    issues: tuple[QualityIssue, ...]

    @property
    def passed(self) -> bool:
        return not self.issues

    @property
    def issue_counts(self) -> dict[str, int]:
        counts = Counter(issue.category for issue in self.issues)
        return dict(sorted(counts.items()))

    @property
    def issue_code_counts(self) -> dict[str, int]:
        counts = Counter(issue.code for issue in self.issues)
        return dict(sorted(counts.items()))

    def to_dict(self) -> dict[str, Any]:
        return {
            "node_count": self.node_count,
            "visible_node_count": self.visible_node_count,
            "interactive_node_count": self.interactive_node_count,
            "minimum_touch_target_dp": self.minimum_touch_target_dp,
            "issue_count": len(self.issues),
            "issue_counts": self.issue_counts,
            "issue_code_counts": self.issue_code_counts,
            "passed": self.passed,
            "issues": [issue.to_dict() for issue in self.issues],
        }


def compare_reference_rgb888_to_candidate_rgb565le(
    reference_rgb888: bytes | bytearray | memoryview,
    candidate_rgb565le: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> PixelComparison:
    """Compare Compose RGB888 with canonical LVGL RGB565LE at native size."""

    reference_rgb565le = rgb888_to_rgb565le(
        reference_rgb888,
        width=width,
        height=height,
    )
    return PixelComparison.from_rgb565(
        compare_rgb565le(
            reference_rgb565le,
            candidate_rgb565le,
            width=width,
            height=height,
        )
    )


def compare_rgb888_images(
    reference_rgb888: bytes | bytearray | memoryview,
    candidate_rgb888: bytes | bytearray | memoryview,
    *,
    width: int,
    height: int,
) -> PixelComparison:
    """Compare two RGB888 frames after canonical RGB565 quantization."""

    reference_rgb565le = rgb888_to_rgb565le(
        reference_rgb888,
        width=width,
        height=height,
    )
    candidate_rgb565le = rgb888_to_rgb565le(
        candidate_rgb888,
        width=width,
        height=height,
    )
    return PixelComparison.from_rgb565(
        compare_rgb565le(
            reference_rgb565le,
            candidate_rgb565le,
            width=width,
            height=height,
        )
    )


def compare_node_evidence(
    reference: dict[str, Any],
    candidate: dict[str, Any],
    *,
    snapshot: dict[str, Any] | None = None,
    bounds_tolerance_px: int = 0,
    bounds_tolerance_dp_q8_8: int = 0,
) -> StructuredComparison:
    """Compare two normalized NodeEvidence documents.

    When a SceneSnapshot is provided, both evidence documents are also checked
    independently against the shared semantic source of truth.
    """

    validate_node_evidence(reference)
    validate_node_evidence(candidate)
    if snapshot is not None:
        validate_scene_snapshot(snapshot)
    _tolerance(bounds_tolerance_px, "bounds_tolerance_px")
    _tolerance(bounds_tolerance_dp_q8_8, "bounds_tolerance_dp_q8_8")

    mismatches: list[Mismatch] = []
    _compare_metadata(reference, candidate, mismatches)

    reference_nodes = _node_map(reference)
    candidate_nodes = _node_map(candidate)
    shared_ids = set(reference_nodes) & set(candidate_nodes)
    for node_id in sorted(set(reference_nodes) - set(candidate_nodes)):
        _append(
            mismatches,
            "node_missing",
            "pair",
            node_id,
            "id",
            node_id,
            None,
        )
    for node_id in sorted(set(candidate_nodes) - set(reference_nodes)):
        _append(
            mismatches,
            "node_unexpected",
            "pair",
            node_id,
            "id",
            None,
            node_id,
        )
    for node_id in sorted(shared_ids):
        _compare_node_pair(
            reference_nodes[node_id],
            candidate_nodes[node_id],
            node_id,
            mismatches,
            bounds_tolerance_px=bounds_tolerance_px,
            bounds_tolerance_dp_q8_8=bounds_tolerance_dp_q8_8,
        )

    if snapshot is not None:
        digest = document_sha256(snapshot)
        for source, evidence, nodes in (
            ("reference_snapshot", reference, reference_nodes),
            ("candidate_snapshot", candidate, candidate_nodes),
        ):
            if evidence["snapshot_sha256"] != digest:
                _append(
                    mismatches,
                    "snapshot_digest",
                    source,
                    None,
                    "snapshot_sha256",
                    digest,
                    evidence["snapshot_sha256"],
                )
            _compare_snapshot_projection(
                snapshot,
                nodes,
                source,
                mismatches,
            )

    ordered = tuple(sorted(mismatches, key=_mismatch_sort_key))
    return StructuredComparison(
        reference_node_count=len(reference_nodes),
        candidate_node_count=len(candidate_nodes),
        compared_node_count=len(shared_ids),
        mismatches=ordered,
    )


def audit_node_evidence_quality(
    evidence: dict[str, Any],
    *,
    minimum_touch_target_dp: int = 48,
) -> NodeEvidenceQualityAudit:
    """Audit one renderer's visible bounds and interactive target sizes.

    Visible nodes are checked in physical pixels against the evidence
    viewport.  A visible node with at least one semantic action is considered
    interactive and its Q8.8 dp bounds must be at least
    ``minimum_touch_target_dp`` in both dimensions.
    """

    validate_node_evidence(evidence)
    if (
        isinstance(minimum_touch_target_dp, bool)
        or not isinstance(minimum_touch_target_dp, int)
        or minimum_touch_target_dp <= 0
    ):
        raise ValueError(
            "minimum_touch_target_dp must be a positive integer"
        )

    viewport_width = evidence["physical_width_px"]
    viewport_height = evidence["physical_height_px"]
    minimum_q8_8 = minimum_touch_target_dp * 256
    issues: list[QualityIssue] = []
    visible_node_count = 0
    interactive_node_count = 0

    for node in evidence["nodes"]:
        if not node["visible"]:
            continue
        visible_node_count += 1
        node_id = node["id"]
        bounds_px = node["bounds_px"]
        if bounds_px["width"] <= 0 or bounds_px["height"] <= 0:
            _append_quality_issue(
                issues,
                "visible_bounds_nonpositive",
                node_id,
                "bounds_px",
                {"width_min": 1, "height_min": 1},
                {
                    "width": bounds_px["width"],
                    "height": bounds_px["height"],
                },
            )
        if (
            bounds_px["x"] < 0
            or bounds_px["y"] < 0
            or bounds_px["x"] + bounds_px["width"] > viewport_width
            or bounds_px["y"] + bounds_px["height"] > viewport_height
        ):
            _append_quality_issue(
                issues,
                "visible_bounds_out_of_viewport",
                node_id,
                "bounds_px",
                {
                    "x_min": 0,
                    "y_min": 0,
                    "x_max": viewport_width,
                    "y_max": viewport_height,
                },
                dict(bounds_px),
            )

        if not node["actions"]:
            continue
        interactive_node_count += 1
        bounds_dp = node["bounds_dp_q8_8"]
        if (
            bounds_dp["width"] < minimum_q8_8
            or bounds_dp["height"] < minimum_q8_8
        ):
            _append_quality_issue(
                issues,
                "interactive_target_too_small",
                node_id,
                "bounds_dp_q8_8",
                {
                    "width_min": minimum_q8_8,
                    "height_min": minimum_q8_8,
                },
                {
                    "width": bounds_dp["width"],
                    "height": bounds_dp["height"],
                },
            )

    ordered = tuple(sorted(issues, key=_quality_issue_sort_key))
    return NodeEvidenceQualityAudit(
        node_count=len(evidence["nodes"]),
        visible_node_count=visible_node_count,
        interactive_node_count=interactive_node_count,
        minimum_touch_target_dp=minimum_touch_target_dp,
        issues=ordered,
    )


def _compare_metadata(
    reference: dict[str, Any],
    candidate: dict[str, Any],
    mismatches: list[Mismatch],
) -> None:
    pairs = (
        (
            "snapshot_identity",
            "snapshot_sha256",
            reference["snapshot_sha256"],
            candidate["snapshot_sha256"],
        ),
        (
            "profile",
            "profile_id",
            reference["profile_id"],
            candidate["profile_id"],
        ),
        (
            "dimensions",
            "physical_width_px",
            reference["physical_width_px"],
            candidate["physical_width_px"],
        ),
        (
            "dimensions",
            "physical_height_px",
            reference["physical_height_px"],
            candidate["physical_height_px"],
        ),
    )
    for code, field, expected, actual in pairs:
        if expected != actual:
            _append(
                mismatches,
                code,
                "pair",
                None,
                field,
                expected,
                actual,
            )

    phase_fields = set(reference["capture_phase"]) | set(
        candidate["capture_phase"]
    )
    for field in sorted(phase_fields):
        expected = reference["capture_phase"].get(field)
        actual = candidate["capture_phase"].get(field)
        if expected != actual:
            _append(
                mismatches,
                "capture_phase",
                "pair",
                None,
                f"capture_phase.{field}",
                expected,
                actual,
            )


def _compare_node_pair(
    reference: dict[str, Any],
    candidate: dict[str, Any],
    node_id: str,
    mismatches: list[Mismatch],
    *,
    bounds_tolerance_px: int,
    bounds_tolerance_dp_q8_8: int,
) -> None:
    _compare_fields(
        reference,
        candidate,
        (
            ("parent_id", "parent"),
            ("role", "role"),
            ("label", "label"),
            ("value", "value"),
            ("state_description", "state_description"),
            ("visible", "visible"),
            ("enabled", "enabled"),
            ("selected", "selected"),
            ("checked", "checked"),
        ),
        "pair",
        node_id,
        mismatches,
    )

    expected_actions = _normalized_actions(reference["actions"])
    actual_actions = _normalized_actions(candidate["actions"])
    if expected_actions != actual_actions:
        _append(
            mismatches,
            "actions",
            "pair",
            node_id,
            "actions",
            expected_actions,
            actual_actions,
        )

    _compare_bounds(
        reference["bounds_px"],
        candidate["bounds_px"],
        "bounds_px",
        bounds_tolerance_px,
        node_id,
        mismatches,
    )
    _compare_bounds(
        reference["bounds_dp_q8_8"],
        candidate["bounds_dp_q8_8"],
        "bounds_dp_q8_8",
        bounds_tolerance_dp_q8_8,
        node_id,
        mismatches,
    )
    _compare_tokens(
        reference["token_roles"],
        candidate["token_roles"],
        node_id,
        mismatches,
    )
    _compare_text(reference.get("text"), candidate.get("text"), node_id, mismatches)


def _compare_snapshot_projection(
    snapshot: dict[str, Any],
    evidence_nodes: dict[str, dict[str, Any]],
    source: str,
    mismatches: list[Mismatch],
) -> None:
    snapshot_nodes = {
        node["id"]: node for node in snapshot["nodes"]
    }
    for node_id in sorted(set(snapshot_nodes) - set(evidence_nodes)):
        _append(
            mismatches,
            "snapshot_node_missing",
            source,
            node_id,
            "id",
            node_id,
            None,
        )
    for node_id in sorted(set(evidence_nodes) - set(snapshot_nodes)):
        _append(
            mismatches,
            "snapshot_node_unexpected",
            source,
            node_id,
            "id",
            None,
            node_id,
        )
    for node_id in sorted(set(snapshot_nodes) & set(evidence_nodes)):
        snapshot_node = snapshot_nodes[node_id]
        evidence_node = evidence_nodes[node_id]
        semantics = snapshot_node["semantics"]
        expected = {
            "parent_id": snapshot_node["parent_id"],
            "role": semantics["role"],
            "label": semantics["label"],
            "value": semantics.get("value", ""),
            "state_description": semantics.get("state_description", ""),
            "visible": snapshot_node["visible"],
            "enabled": snapshot_node["enabled"],
        }
        projection_fields = [
            ("parent_id", "parent"),
            ("role", "role"),
            ("label", "label"),
            ("value", "value"),
            ("state_description", "state_description"),
            ("visible", "visible"),
            ("enabled", "enabled"),
        ]
        for state_field in ("selected", "checked"):
            if state_field in snapshot_node["props"]:
                expected[state_field] = snapshot_node["props"][state_field]
                projection_fields.append((state_field, state_field))
        _compare_fields(
            expected,
            evidence_node,
            projection_fields,
            source,
            node_id,
            mismatches,
        )
        expected_actions = _normalized_actions(snapshot_node["actions"])
        actual_actions = _normalized_actions(evidence_node["actions"])
        if expected_actions != actual_actions:
            _append(
                mismatches,
                "actions",
                source,
                node_id,
                "actions",
                expected_actions,
                actual_actions,
            )


def _compare_fields(
    reference: dict[str, Any],
    candidate: dict[str, Any],
    fields: Iterable[tuple[str, str]],
    source: str,
    node_id: str,
    mismatches: list[Mismatch],
) -> None:
    for field, code in fields:
        expected = reference.get(field)
        actual = candidate.get(field)
        if expected != actual:
            _append(
                mismatches,
                code,
                source,
                node_id,
                field,
                expected,
                actual,
            )


def _compare_bounds(
    reference: dict[str, int],
    candidate: dict[str, int],
    kind: str,
    tolerance: int,
    node_id: str,
    mismatches: list[Mismatch],
) -> None:
    for coordinate in ("x", "y", "width", "height"):
        expected = reference[coordinate]
        actual = candidate[coordinate]
        if abs(expected - actual) > tolerance:
            _append(
                mismatches,
                kind,
                "pair",
                node_id,
                f"{kind}.{coordinate}",
                expected,
                actual,
            )


def _compare_tokens(
    reference: dict[str, str],
    candidate: dict[str, str],
    node_id: str,
    mismatches: list[Mismatch],
) -> None:
    for role in sorted(set(reference) - set(candidate)):
        _append(
            mismatches,
            "token_missing",
            "pair",
            node_id,
            f"token_roles.{role}",
            reference[role],
            None,
        )
    for role in sorted(set(candidate) - set(reference)):
        _append(
            mismatches,
            "token_unexpected",
            "pair",
            node_id,
            f"token_roles.{role}",
            None,
            candidate[role],
        )
    for role in sorted(set(reference) & set(candidate)):
        if reference[role] != candidate[role]:
            _append(
                mismatches,
                "token_value",
                "pair",
                node_id,
                f"token_roles.{role}",
                reference[role],
                candidate[role],
            )


def _compare_text(
    reference: dict[str, Any] | None,
    candidate: dict[str, Any] | None,
    node_id: str,
    mismatches: list[Mismatch],
) -> None:
    if (reference is None) != (candidate is None):
        _append(
            mismatches,
            "text_presence",
            "pair",
            node_id,
            "text",
            reference is not None,
            candidate is not None,
        )
        return
    if reference is None or candidate is None:
        return
    _compare_fields(
        reference,
        candidate,
        (
            ("line_count", "line_count"),
            ("truncated", "truncated"),
            ("baselines_px", "baselines_px"),
        ),
        "pair",
        node_id,
        mismatches,
    )


def _append(
    mismatches: list[Mismatch],
    code: str,
    source: str,
    node_id: str | None,
    field: str,
    expected: Any,
    actual: Any,
) -> None:
    mismatches.append(
        Mismatch(
            category=MISMATCH_TAXONOMY[code],
            code=code,
            source=source,
            node_id=node_id,
            field=field,
            expected=expected,
            actual=actual,
        )
    )


def _append_quality_issue(
    issues: list[QualityIssue],
    code: str,
    node_id: str,
    field: str,
    expected: Any,
    actual: Any,
) -> None:
    issues.append(
        QualityIssue(
            category=QUALITY_ISSUE_TAXONOMY[code],
            code=code,
            node_id=node_id,
            field=field,
            expected=expected,
            actual=actual,
        )
    )


def _node_map(evidence: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {node["id"]: node for node in evidence["nodes"]}


def _normalized_actions(actions: list[dict[str, str]]) -> list[dict[str, str]]:
    return sorted(
        (
            {
                "kind": action["kind"],
                "action_id": action["action_id"],
            }
            for action in actions
        ),
        key=lambda action: (action["kind"], action["action_id"]),
    )


def _mismatch_sort_key(mismatch: Mismatch) -> tuple[bytes, ...]:
    return (
        mismatch.category.encode("utf-8"),
        mismatch.code.encode("utf-8"),
        mismatch.source.encode("utf-8"),
        (mismatch.node_id or "").encode("utf-8"),
        mismatch.field.encode("utf-8"),
        canonical_json_bytes(mismatch.expected),
        canonical_json_bytes(mismatch.actual),
    )


def _quality_issue_sort_key(issue: QualityIssue) -> tuple[bytes, ...]:
    return (
        issue.category.encode("utf-8"),
        issue.code.encode("utf-8"),
        issue.node_id.encode("utf-8"),
        issue.field.encode("utf-8"),
        canonical_json_bytes(issue.expected),
        canonical_json_bytes(issue.actual),
    )


def _tolerance(value: int, name: str) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"{name} must be a non-negative integer")
