from __future__ import annotations

import time
from pathlib import Path
from types import SimpleNamespace

from pptx import Presentation

from doodad_agent.codex_work_worker import CodexWorkBuilder
from doodad_agent.jobs import JobManager
from doodad_agent.storage import Store


class FakeClient:
    def run_turn(
        self,
        *,
        workspace: Path,
        prompt: str,
        thread_id: str | None,
        stop,
        on_started,
        on_question,
    ):
        del thread_id, stop, on_question
        on_started("thread-work", "turn-work")
        name = "report.md" if "report.md" in prompt else "presentation.md"
        (workspace / name).write_text(f"# {name}\n\nCompleted output.\n", encoding="utf-8")
        return SimpleNamespace(
            status="completed",
            error=None,
            thread_id="thread-work",
            turn_id="turn-work",
        )

    def close(self) -> None:
        return None


class RecordingDelivery:
    def __init__(self) -> None:
        self.deliveries: list[tuple[str, str]] = []

    def deliver(self, artifact: Path, recipient: str, subject: str) -> None:
        del subject
        self.deliveries.append((artifact.name, recipient))


def wait_for(predicate, timeout: float = 2.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("background work did not finish")


def test_codex_research_and_presentation_delivery_run_concurrently(tmp_path: Path) -> None:
    store = Store(tmp_path / "work.db")
    jobs = JobManager(store, "t-watch-work")
    delivery = RecordingDelivery()
    builder = CodexWorkBuilder(
        jobs,
        tmp_path / "workspaces",
        "unused-codex",
        delivery=delivery,
        client_factory=FakeClient,
        max_concurrent=2,
    )
    try:
        research = builder.start_work(
            "research_report", "Compare local-first agent interfaces", 1_000
        )
        slides = builder.start_work(
            "presentation_delivery",
            "Summarize the recommendation as six slides",
            1_001,
            recipient="pat@example.com",
        )
        wait_for(lambda: all(
            jobs.job(job_id)["state"] == "completed"
            for job_id in (research, slides)
        ))

        assert delivery.deliveries == [("presentation.pptx", "pat@example.com")]
        assert (tmp_path / "workspaces" / research / "report.md").is_file()
        deck = tmp_path / "workspaces" / slides / "presentation.pptx"
        assert deck.is_file()
        assert len(Presentation(deck).slides) == 1
        assert jobs.task_status(2_000, "research report")[0]["status"] == "COMPLETED"
        assert jobs.task_status(2_000, "slide deck")[0]["status"] == "COMPLETED"
    finally:
        builder.close()
        store.close()


def test_presentation_never_claims_email_without_delivery_configuration(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "no-email.db")
    jobs = JobManager(store, "t-watch-no-email")
    builder = CodexWorkBuilder(
        jobs,
        tmp_path / "workspaces",
        "unused-codex",
        client_factory=FakeClient,
    )
    try:
        job_id = builder.start_work(
            "presentation_delivery",
            "Create a short slide deck",
            1_000,
            recipient="pat@example.com",
        )
        wait_for(lambda: jobs.job(job_id)["state"] == "failed")
        assert (tmp_path / "workspaces" / job_id / "presentation.pptx").is_file()
        assert jobs.events(job_id)[-1].summary == (
            "Background work stopped at a controlled gate."
        )
        assert "not configured" in jobs.events(job_id)[-1].payload["reason"]
    finally:
        builder.close()
        store.close()
