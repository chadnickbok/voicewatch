from __future__ import annotations

import json
import tempfile
import unittest
from html.parser import HTMLParser
from pathlib import Path

from tools.doodad_cli.parallax_compare import (
    Mismatch,
    PixelComparison,
    StructuredComparison,
)
from tools.doodad_cli.parallax_report import (
    ComparisonCaseReport,
    ParallaxReport,
    validate_relative_link,
    write_static_report,
)


ZERO_HASH = "0" * 64
IMAGE_NAMES = (
    "reference",
    "candidate",
    "side_by_side",
    "overlay",
    "difference",
)


class LinkCollector(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.links: list[str] = []

    def handle_starttag(
        self,
        tag: str,
        attrs: list[tuple[str, str | None]],
    ) -> None:
        values = dict(attrs)
        for name in ("href", "src"):
            if values.get(name):
                self.links.append(values[name] or "")


def pixel_fixture(*, changed: int = 0) -> PixelComparison:
    return PixelComparison(
        width=2,
        height=2,
        pixel_count=4,
        changed_pixels=changed,
        absolute_error_sum=changed * 765,
        squared_error_sum=changed * 195_075,
        max_channel_error=255 if changed else 0,
    )


def structured_fixture(*, malicious: bool = False) -> StructuredComparison:
    if not malicious:
        return StructuredComparison(
            reference_node_count=2,
            candidate_node_count=2,
            compared_node_count=2,
            mismatches=(),
        )
    return StructuredComparison(
        reference_node_count=2,
        candidate_node_count=2,
        compared_node_count=2,
        mismatches=(
            Mismatch(
                category="semantics",
                code="label",
                source="pair",
                node_id="timer.start",
                field="label",
                expected='</code><script>alert("expected")</script>',
                actual="<b>actual & different</b>",
            ),
        ),
    )


def artifact_map(*, strange_name: bool = False) -> dict[str, str]:
    suffix = "<&.png" if strange_name else ".png"
    artifacts = {
        name: f"assets/{name}{suffix}" for name in IMAGE_NAMES
    }
    artifacts["reference_evidence"] = "evidence/reference.json"
    artifacts["candidate_evidence"] = "evidence/candidate.json"
    return artifacts


def create_artifacts(root: Path, artifacts: dict[str, str]) -> None:
    for index, relative in enumerate(artifacts.values()):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f"artifact-{index}".encode())


def case_fixture(
    *,
    case_id: str = "timer.primary",
    app_slug: str = "timer",
    title: str = "Timer",
    artifacts: dict[str, str] | None = None,
    changed: int = 0,
    malicious: bool = False,
) -> ComparisonCaseReport:
    return ComparisonCaseReport(
        case_id=case_id,
        app_slug=app_slug,
        title=title,
        sequence=4,
        snapshot_sha256=ZERO_HASH,
        profile_id="watch_square_240",
        capture_phase="resting",
        comparison_policy="parallax_exact",
        pixel=pixel_fixture(changed=changed),
        structured=structured_fixture(malicious=malicious),
        artifacts=artifacts or artifact_map(),
    )


class ParallaxReportTests(unittest.TestCase):
    def test_static_report_writes_json_and_resolvable_relative_links(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = artifact_map()
            create_artifacts(root, artifacts)
            paths = write_static_report(
                root,
                ParallaxReport(
                    report_id="parallax.twenty",
                    title="Project Parallax",
                    cases=[case_fixture(artifacts=artifacts)],
                ),
            )
            self.assertEqual(paths.html, root / "index.html")
            self.assertEqual(paths.json, root / "report.json")
            document = json.loads(paths.json.read_text())
            self.assertEqual(
                document["summary"],
                {
                    "case_count": 1,
                    "different_count": 0,
                    "exact_count": 1,
                    "quality_issue_count": 0,
                    "quality_passed_count": 1,
                },
            )
            collector = LinkCollector()
            collector.feed(paths.html.read_text())
            self.assertIn("report.json", collector.links)
            for link in collector.links:
                decoded = link.replace("%3C", "<").replace("%26", "&")
                self.assertFalse(Path(decoded).is_absolute())
                self.assertNotIn("..", Path(decoded).parts)
                self.assertTrue((root / decoded).is_file())

    def test_html_escapes_titles_mismatch_values_and_artifact_links(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = artifact_map(strange_name=True)
            create_artifacts(root, artifacts)
            paths = write_static_report(
                root,
                ParallaxReport(
                    report_id="parallax.escape",
                    title='<Project & "Parallax">',
                    cases=[
                        case_fixture(
                            title='<script>alert("title")</script>',
                            artifacts=artifacts,
                            changed=1,
                            malicious=True,
                        )
                    ],
                ),
            )
            rendered = paths.html.read_text()
            self.assertNotIn("<script>", rendered)
            self.assertNotIn("<b>actual", rendered)
            self.assertIn("&lt;script&gt;", rendered)
            self.assertIn("&lt;b&gt;actual &amp; different&lt;/b&gt;", rendered)
            self.assertIn("%3C%26.png", rendered)
            document = json.loads(paths.json.read_text())
            self.assertEqual(document["summary"]["different_count"], 1)

    def test_report_order_and_bytes_are_deterministic_across_two_runs(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            roots = (base / "first", base / "second")
            cases = [
                case_fixture(
                    case_id="weather.primary",
                    app_slug="weather",
                    title="Weather",
                ),
                case_fixture(),
            ]
            outputs = []
            for root in roots:
                create_artifacts(root, artifact_map())
                outputs.append(
                    write_static_report(
                        root,
                        ParallaxReport(
                            report_id="parallax.deterministic",
                            title="Project Parallax",
                            cases=list(reversed(cases)),
                        ),
                    )
                )
            self.assertEqual(
                outputs[0].html.read_bytes(),
                outputs[1].html.read_bytes(),
            )
            self.assertEqual(
                outputs[0].json.read_bytes(),
                outputs[1].json.read_bytes(),
            )
            document = json.loads(outputs[0].json.read_text())
            self.assertEqual(
                [case["app_slug"] for case in document["cases"]],
                ["timer", "weather"],
            )

    def test_static_index_handles_twenty_aligned_pairs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = artifact_map()
            create_artifacts(root, artifacts)
            cases = [
                case_fixture(
                    case_id=f"app{index:02d}.primary",
                    app_slug=f"app{index:02d}",
                    title=f"App {index:02d}",
                    artifacts=artifacts,
                    changed=index % 2,
                )
                for index in range(20)
            ]
            paths = write_static_report(
                root,
                ParallaxReport(
                    report_id="parallax.twenty",
                    title="Twenty pairs",
                    cases=list(reversed(cases)),
                ),
            )
            document = json.loads(paths.json.read_text())
            self.assertEqual(document["summary"]["case_count"], 20)
            self.assertEqual(document["summary"]["exact_count"], 10)
            self.assertEqual(document["summary"]["different_count"], 10)
            rendered = paths.html.read_text()
            self.assertEqual(rendered.count('<section class="case"'), 20)
            self.assertLess(
                rendered.index('id="app00.primary"'),
                rendered.index('id="app19.primary"'),
            )

    def test_unsafe_absolute_parent_url_and_windows_links_are_rejected(
        self,
    ) -> None:
        for link in (
            "/tmp/image.png",
            "../image.png",
            "assets/../image.png",
            "https://example.test/image.png",
            "assets/image.png?version=1",
            "assets\\image.png",
        ):
            with self.subTest(link=link):
                with self.assertRaisesRegex(ValueError, "artifact link"):
                    validate_relative_link(link)

    def test_missing_artifact_is_rejected_before_report_files_are_written(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = artifact_map()
            create_artifacts(root, artifacts)
            missing = root / artifacts["difference"]
            missing.unlink()
            with self.assertRaisesRegex(ValueError, "does not exist"):
                write_static_report(
                    root,
                    ParallaxReport(
                        report_id="parallax.missing",
                        title="Missing",
                        cases=[case_fixture(artifacts=artifacts)],
                    ),
                )
            self.assertFalse((root / "index.html").exists())
            self.assertFalse((root / "report.json").exists())

    def test_symlink_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / "report"
            artifacts = artifact_map()
            create_artifacts(root, artifacts)
            outside = base / "outside.png"
            outside.write_bytes(b"outside")
            escaped = root / artifacts["difference"]
            escaped.unlink()
            escaped.symlink_to(outside)
            with self.assertRaisesRegex(ValueError, "escapes"):
                write_static_report(
                    root,
                    ParallaxReport(
                        report_id="parallax.symlink",
                        title="Symlink",
                        cases=[case_fixture(artifacts=artifacts)],
                    ),
                )

    def test_missing_required_derivative_is_rejected(self) -> None:
        artifacts = artifact_map()
        del artifacts["overlay"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            create_artifacts(root, artifacts)
            with self.assertRaisesRegex(ValueError, "missing artifacts"):
                write_static_report(
                    root,
                    ParallaxReport(
                        report_id="parallax.incomplete",
                        title="Incomplete",
                        cases=[case_fixture(artifacts=artifacts)],
                    ),
                )

    def test_duplicate_case_ids_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            create_artifacts(root, artifact_map())
            with self.assertRaisesRegex(ValueError, "duplicate case_id"):
                write_static_report(
                    root,
                    ParallaxReport(
                        report_id="parallax.duplicate",
                        title="Duplicate",
                        cases=[case_fixture(), case_fixture()],
                    ),
                )

    def test_verify_links_can_be_deferred_for_orchestrator_staging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = write_static_report(
                root,
                ParallaxReport(
                    report_id="parallax.staged",
                    title="Staged",
                    cases=[case_fixture()],
                ),
                verify_links=False,
            )
            self.assertTrue(paths.html.is_file())
            self.assertTrue(paths.json.is_file())


if __name__ == "__main__":
    unittest.main()
