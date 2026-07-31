from __future__ import annotations

import copy
import unittest

from tools.doodad_cli.parallax_compare import (
    MISMATCH_TAXONOMY,
    QUALITY_ISSUE_TAXONOMY,
    audit_node_evidence_quality,
    compare_node_evidence,
    compare_reference_rgb888_to_candidate_rgb565le,
    compare_rgb888_images,
)
from tools.doodad_cli.parallax_contract import document_sha256


ZERO_HASH = "0" * 64


def snapshot_fixture() -> dict:
    return {
        "schema_version": 1,
        "app_id": "timer",
        "screen_id": "timer.home",
        "origin": "guest_appspec",
        "nodes": [
            {
                "id": "timer.home",
                "parent_id": None,
                "kind": "screen",
                "depth": 0,
                "child_count": 2,
                "visible": True,
                "enabled": True,
                "props": {"gap": "sm", "alignment": "stretch"},
                "semantics": {"role": "screen", "label": "Timer"},
                "actions": [],
            },
            {
                "id": "timer.label",
                "parent_id": "timer.home",
                "kind": "text",
                "depth": 1,
                "child_count": 0,
                "visible": True,
                "enabled": True,
                "props": {
                    "primary_text": "05:00",
                    "variant": "numeral",
                    "alignment": "center",
                    "max_lines": 1,
                },
                "semantics": {
                    "role": "text",
                    "label": "Five minutes",
                    "value": "05:00",
                },
                "actions": [],
            },
            {
                "id": "timer.start",
                "parent_id": "timer.home",
                "kind": "button",
                "depth": 1,
                "child_count": 0,
                "visible": True,
                "enabled": True,
                "props": {
                    "primary_text": "Start",
                    "variant": "filled",
                    "tone": "primary",
                    "size": "default",
                },
                "semantics": {"role": "button", "label": "Start timer"},
                "actions": [
                    {"kind": "tap", "action_id": "timer.start"}
                ],
            },
        ],
    }


def _bounds(x: int, y: int, width: int, height: int) -> dict:
    return {"x": x, "y": y, "width": width, "height": height}


def evidence_fixture(kind: str) -> dict:
    snapshot = snapshot_fixture()
    renderer = (
        {
            "kind": "compose",
            "mode": "host",
            "version": "1.6.2",
            "build_sha256": ZERO_HASH,
        }
        if kind == "compose"
        else {
            "kind": "lvgl",
            "mode": "simulator",
            "version": "9.5.0",
            "build_sha256": ZERO_HASH,
        }
    )
    return {
        "schema_version": 1,
        "snapshot_sha256": document_sha256(snapshot),
        "capture_phase": {
            "id": "resting",
            "state": "resting",
            "animation_fraction_milli": 0,
        },
        "renderer": renderer,
        "profile_id": "watch_square_240",
        "physical_width_px": 240,
        "physical_height_px": 240,
        "nodes": [
            {
                "id": "timer.home",
                "parent_id": None,
                "role": "screen",
                "label": "Timer",
                "value": "",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [],
                "bounds_px": _bounds(0, 0, 240, 240),
                "bounds_dp_q8_8": _bounds(0, 0, 192 * 256, 192 * 256),
                "token_roles": {"background": "background"},
            },
            {
                "id": "timer.label",
                "parent_id": "timer.home",
                "role": "text",
                "label": "Five minutes",
                "value": "05:00",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [],
                "bounds_px": _bounds(20, 40, 200, 50),
                "bounds_dp_q8_8": _bounds(
                    16 * 256,
                    32 * 256,
                    160 * 256,
                    40 * 256,
                ),
                "token_roles": {"text_color": "on_background"},
                "text": {
                    "line_count": 1,
                    "truncated": False,
                    "baselines_px": [74],
                },
            },
            {
                "id": "timer.start",
                "parent_id": "timer.home",
                "role": "button",
                "label": "Start timer",
                "value": "",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [
                    {"kind": "tap", "action_id": "timer.start"}
                ],
                "bounds_px": _bounds(20, 170, 200, 52),
                "bounds_dp_q8_8": _bounds(
                    16 * 256,
                    136 * 256,
                    160 * 256,
                    42 * 256,
                ),
                "token_roles": {
                    "container_color": "primary",
                    "content_color": "on_primary",
                },
                "text": {
                    "line_count": 1,
                    "truncated": False,
                    "baselines_px": [201],
                },
            },
        ],
    }


class ParallaxPixelComparisonTests(unittest.TestCase):
    def test_reference_rgb888_is_quantized_before_rgb565_comparison(self) -> None:
        reference = bytes(
            (
                255,
                0,
                0,
                0,
                255,
                0,
                0,
                0,
                255,
                255,
                255,
                255,
            )
        )
        metrics = compare_reference_rgb888_to_candidate_rgb565le(
            reference,
            bytes.fromhex("00f8 e007 1f00 ffff"),
            width=2,
            height=2,
        )
        self.assertTrue(metrics.exact)
        self.assertEqual(metrics.changed_pixels, 0)
        self.assertEqual(metrics.to_dict()["channel_samples"], 12)
        self.assertEqual(metrics.to_dict()["mse"], 0.0)
        self.assertEqual(metrics.to_dict()["rmse"], 0.0)

    def test_rgb888_comparison_exposes_exact_metric_totals(self) -> None:
        reference = bytes(12)
        candidate = bytes((255, 255, 255) + (0, 0, 0) * 3)
        metrics = compare_rgb888_images(
            reference,
            candidate,
            width=2,
            height=2,
        )
        self.assertFalse(metrics.exact)
        self.assertEqual(metrics.pixel_count, 4)
        self.assertEqual(metrics.changed_pixels, 1)
        self.assertEqual(metrics.absolute_error_sum, 765)
        self.assertEqual(metrics.max_channel_error, 255)

    def test_pixel_comparison_rejects_length_mismatch_instead_of_resizing(
        self,
    ) -> None:
        with self.assertRaisesRegex(ValueError, "expected 12"):
            compare_rgb888_images(
                bytes(12),
                bytes(11),
                width=2,
                height=2,
            )


class ParallaxStructuredComparisonTests(unittest.TestCase):
    def test_identical_normalized_evidence_and_snapshot_are_exact(self) -> None:
        comparison = compare_node_evidence(
            evidence_fixture("compose"),
            evidence_fixture("lvgl"),
            snapshot=snapshot_fixture(),
        )
        self.assertTrue(comparison.exact)
        self.assertEqual(comparison.reference_node_count, 3)
        self.assertEqual(comparison.candidate_node_count, 3)
        self.assertEqual(comparison.compared_node_count, 3)
        self.assertEqual(comparison.mismatch_counts, {})

    def test_pair_mismatch_taxonomy_covers_each_structured_dimension(
        self,
    ) -> None:
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        candidate["profile_id"] = "watch_round_small"
        candidate["physical_width_px"] = 192
        candidate["capture_phase"] = {
            "id": "pressed",
            "state": "pressed",
            "animation_fraction_milli": 1000,
            "target": "timer.start",
        }
        start = candidate["nodes"][2]
        start["parent_id"] = "timer.label"
        start["role"] = "toggle"
        start["label"] = "Begin"
        start["value"] = "armed"
        start["state_description"] = "selected"
        start["visible"] = False
        start["enabled"] = False
        start["selected"] = True
        start["checked"] = True
        start["actions"] = [
            {"kind": "long_press", "action_id": "timer.start"}
        ]
        start["bounds_px"]["x"] += 2
        start["bounds_dp_q8_8"]["width"] += 2
        start["token_roles"] = {
            "container_color": "secondary",
            "border_color": "outline",
        }
        start["text"] = {
            "line_count": 2,
            "truncated": True,
            "baselines_px": [190, 210],
        }
        candidate["nodes"][1].pop("text")

        comparison = compare_node_evidence(reference, candidate)
        categories = set(comparison.mismatch_counts)
        self.assertEqual(
            categories,
            {
                "metadata",
                "hierarchy",
                "semantics",
                "state",
                "actions",
                "bounds",
                "tokens",
                "text_layout",
            },
        )
        codes = {mismatch.code for mismatch in comparison.mismatches}
        self.assertTrue(
            {
                "profile",
                "dimensions",
                "capture_phase",
                "parent",
                "role",
                "label",
                "value",
                "state_description",
                "visible",
                "enabled",
                "selected",
                "checked",
                "actions",
                "bounds_px",
                "bounds_dp_q8_8",
                "token_missing",
                "token_unexpected",
                "token_value",
                "text_presence",
                "line_count",
                "truncated",
                "baselines_px",
            }
            <= codes
        )
        self.assertTrue(codes <= set(MISMATCH_TAXONOMY))

    def test_missing_and_unexpected_nodes_are_distinct(self) -> None:
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        candidate["nodes"].pop()
        candidate["nodes"].append(
            {
                "id": "timer.cancel",
                "parent_id": "timer.home",
                "role": "button",
                "label": "Cancel",
                "value": "",
                "state_description": "",
                "visible": True,
                "enabled": True,
                "actions": [
                    {"kind": "tap", "action_id": "timer.cancel"}
                ],
                "bounds_px": _bounds(20, 170, 200, 52),
                "bounds_dp_q8_8": _bounds(
                    16 * 256,
                    136 * 256,
                    160 * 256,
                    42 * 256,
                ),
                "token_roles": {"container_color": "secondary"},
            }
        )
        comparison = compare_node_evidence(reference, candidate)
        self.assertEqual(
            {
                (mismatch.code, mismatch.node_id)
                for mismatch in comparison.mismatches
            },
            {
                ("node_missing", "timer.start"),
                ("node_unexpected", "timer.cancel"),
            },
        )

    def test_snapshot_projection_identifies_both_renderer_sources(self) -> None:
        snapshot = snapshot_fixture()
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        reference["nodes"][1]["label"] = "Wrong"
        candidate["nodes"][1]["label"] = "Wrong"
        comparison = compare_node_evidence(
            reference,
            candidate,
            snapshot=snapshot,
        )
        self.assertEqual(
            {
                (mismatch.source, mismatch.code)
                for mismatch in comparison.mismatches
            },
            {
                ("reference_snapshot", "label"),
                ("candidate_snapshot", "label"),
            },
        )

    def test_renderer_local_selected_state_is_not_projected_from_snapshot(
        self,
    ) -> None:
        snapshot = snapshot_fixture()
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        for evidence in (reference, candidate):
            evidence["capture_phase"] = {
                "id": "selected",
                "state": "selected",
                "animation_fraction_milli": 1000,
                "target": "timer.start",
            }
            evidence["nodes"][2]["selected"] = True
        comparison = compare_node_evidence(
            reference,
            candidate,
            snapshot=snapshot,
        )
        self.assertTrue(comparison.exact)

    def test_snapshot_digest_mismatch_is_reported_not_silently_accepted(
        self,
    ) -> None:
        snapshot = snapshot_fixture()
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        reference["snapshot_sha256"] = "1" * 64
        candidate["snapshot_sha256"] = "1" * 64
        comparison = compare_node_evidence(
            reference,
            candidate,
            snapshot=snapshot,
        )
        self.assertEqual(
            [
                (mismatch.source, mismatch.code)
                for mismatch in comparison.mismatches
            ],
            [
                ("candidate_snapshot", "snapshot_digest"),
                ("reference_snapshot", "snapshot_digest"),
            ],
        )

    def test_bounds_tolerances_are_inclusive_and_do_not_resize(self) -> None:
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        candidate["nodes"][2]["bounds_px"]["x"] += 1
        candidate["nodes"][2]["bounds_dp_q8_8"]["x"] += 2
        comparison = compare_node_evidence(
            reference,
            candidate,
            bounds_tolerance_px=1,
            bounds_tolerance_dp_q8_8=2,
        )
        self.assertTrue(comparison.exact)
        candidate["physical_height_px"] = 241
        comparison = compare_node_evidence(
            reference,
            candidate,
            bounds_tolerance_px=1,
            bounds_tolerance_dp_q8_8=2,
        )
        self.assertEqual(
            [(mismatch.code, mismatch.field) for mismatch in comparison.mismatches],
            [("dimensions", "physical_height_px")],
        )

    def test_actions_are_compared_as_a_semantic_set(self) -> None:
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        extra = {"kind": "long_press", "action_id": "timer.start"}
        reference["nodes"][2]["actions"].append(copy.deepcopy(extra))
        candidate["nodes"][2]["actions"].insert(0, copy.deepcopy(extra))
        comparison = compare_node_evidence(reference, candidate)
        self.assertTrue(comparison.exact)

    def test_mismatch_output_order_is_stable(self) -> None:
        reference = evidence_fixture("compose")
        candidate = evidence_fixture("lvgl")
        candidate["nodes"][2]["label"] = "Different"
        candidate["nodes"][1]["value"] = "04:59"
        first = compare_node_evidence(reference, candidate).to_dict()
        second = compare_node_evidence(reference, candidate).to_dict()
        self.assertEqual(first, second)


class ParallaxNodeEvidenceQualityAuditTests(unittest.TestCase):
    def test_in_viewport_positive_bounds_and_48dp_target_pass(self) -> None:
        evidence = evidence_fixture("lvgl")
        evidence["nodes"][2]["bounds_dp_q8_8"]["height"] = 48 * 256
        audit = audit_node_evidence_quality(evidence)
        self.assertTrue(audit.passed)
        self.assertEqual(audit.node_count, 3)
        self.assertEqual(audit.visible_node_count, 3)
        self.assertEqual(audit.interactive_node_count, 1)
        self.assertEqual(audit.issue_counts, {})
        self.assertEqual(audit.issue_code_counts, {})
        self.assertEqual(
            audit.to_dict(),
            {
                "node_count": 3,
                "visible_node_count": 3,
                "interactive_node_count": 1,
                "minimum_touch_target_dp": 48,
                "issue_count": 0,
                "issue_counts": {},
                "issue_code_counts": {},
                "passed": True,
                "issues": [],
            },
        )

    def test_audit_reports_each_stable_quality_issue_code(self) -> None:
        evidence = evidence_fixture("compose")
        label = evidence["nodes"][1]
        label["bounds_px"] = _bounds(-1, 40, 0, 50)
        start = evidence["nodes"][2]
        start["bounds_dp_q8_8"]["height"] = 47 * 256

        audit = audit_node_evidence_quality(evidence)

        self.assertFalse(audit.passed)
        self.assertEqual(audit.issue_counts, {"geometry": 2, "touch_target": 1})
        self.assertEqual(
            audit.issue_code_counts,
            {
                "interactive_target_too_small": 1,
                "visible_bounds_nonpositive": 1,
                "visible_bounds_out_of_viewport": 1,
            },
        )
        self.assertEqual(
            {issue.code for issue in audit.issues},
            set(QUALITY_ISSUE_TAXONOMY),
        )
        self.assertEqual(
            {
                issue.code: (issue.node_id, issue.field)
                for issue in audit.issues
            },
            {
                "visible_bounds_nonpositive": (
                    "timer.label",
                    "bounds_px",
                ),
                "visible_bounds_out_of_viewport": (
                    "timer.label",
                    "bounds_px",
                ),
                "interactive_target_too_small": (
                    "timer.start",
                    "bounds_dp_q8_8",
                ),
            },
        )
        touch_issue = next(
            issue
            for issue in audit.issues
            if issue.code == "interactive_target_too_small"
        )
        self.assertEqual(
            touch_issue.expected,
            {"width_min": 48 * 256, "height_min": 48 * 256},
        )
        self.assertEqual(
            touch_issue.actual,
            {"width": 160 * 256, "height": 47 * 256},
        )

    def test_only_visible_nodes_are_audited(self) -> None:
        evidence = evidence_fixture("lvgl")
        label = evidence["nodes"][1]
        label["visible"] = False
        label["bounds_px"] = _bounds(-10, -20, 0, 0)
        start = evidence["nodes"][2]
        start["visible"] = False
        start["bounds_dp_q8_8"] = _bounds(0, 0, 1, 1)

        audit = audit_node_evidence_quality(evidence)

        self.assertTrue(audit.passed)
        self.assertEqual(audit.visible_node_count, 1)
        self.assertEqual(audit.interactive_node_count, 0)

    def test_custom_minimum_is_inclusive_and_strictly_validated(self) -> None:
        evidence = evidence_fixture("lvgl")
        evidence["nodes"][2]["bounds_dp_q8_8"] = _bounds(
            0,
            0,
            42 * 256,
            42 * 256,
        )
        self.assertTrue(
            audit_node_evidence_quality(
                evidence,
                minimum_touch_target_dp=42,
            ).passed
        )
        issue = audit_node_evidence_quality(
            evidence,
            minimum_touch_target_dp=43,
        ).issues[0]
        self.assertEqual(issue.code, "interactive_target_too_small")
        for invalid in (0, -1, True, 48.0):
            with self.subTest(invalid=invalid):
                with self.assertRaisesRegex(ValueError, "positive integer"):
                    audit_node_evidence_quality(
                        evidence,
                        minimum_touch_target_dp=invalid,  # type: ignore[arg-type]
                    )

    def test_issue_output_order_is_stable(self) -> None:
        evidence = evidence_fixture("lvgl")
        evidence["nodes"][1]["bounds_px"] = _bounds(-1, 0, 0, 50)
        first = audit_node_evidence_quality(evidence).to_dict()
        second = audit_node_evidence_quality(evidence).to_dict()
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
