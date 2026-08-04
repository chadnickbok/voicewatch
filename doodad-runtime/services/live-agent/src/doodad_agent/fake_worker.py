"""Deterministic durable fake app builder with an injected clock."""

from __future__ import annotations

from dataclasses import dataclass

from .jobs import JobManager


@dataclass
class ManualClock:
    now_ms: int = 0

    def advance(self, milliseconds: int) -> int:
        self.now_ms += milliseconds
        return self.now_ms


class FakeAppBuilder:
    QUESTION_DELAY_MS = 10_000
    COMPLETION_DELAY_MS = 30_000

    def __init__(self, jobs: JobManager) -> None:
        self.jobs = jobs

    def start(self, brief: str, now_ms: int) -> str:
        job_id = self.jobs.create("fake_app_build", {"brief": brief}, now_ms)
        self.jobs.append(
            job_id, "started", "The rest-timer build is running.",
            {"progress": 0}, "fake-builder", now_ms,
        )
        return job_id

    def tick(self, now_ms: int) -> list[str]:
        changed: list[str] = []
        rows = self.jobs.store.fetch_all(
            "SELECT * FROM jobs WHERE device_id=? AND kind='fake_app_build' "
            "AND state NOT IN ('completed','failed','cancelled') ORDER BY created_at_ms",
            (self.jobs.device_id,),
        )
        for row in rows:
            job_id = str(row["job_id"])
            events = self.jobs.events(job_id)
            kinds = [event.kind for event in events]
            if "needs_input" not in kinds and now_ms - int(row["created_at_ms"]) >= self.QUESTION_DELAY_MS:
                self.jobs.append(
                    job_id,
                    "needs_input",
                    "The timer works; the builder needs a layout choice.",
                    {"question": {"id": "layout", "prompt": "Should the timer use a ring or a horizontal bar?", "answer_schema": {"type": "string", "enum": ["ring", "bar"]}}},
                    "fake-builder",
                    now_ms,
                )
                changed.append(job_id)
                continue
            answer = next((event for event in events if event.kind == "input_received"), None)
            if answer and "completed" not in kinds and now_ms - answer.created_at_ms >= self.COMPLETION_DELAY_MS:
                self.jobs.append(
                    job_id, "progress", "The rest timer passed its deterministic checks.",
                    {"progress": 100}, "fake-builder", now_ms,
                )
                self.jobs.append(
                    job_id, "completed", "Your rest-timer design is ready.",
                    {"layout": answer.payload["answer"]}, "fake-builder", now_ms,
                )
                changed.append(job_id)
        return changed

    def close(self) -> None:
        """Match the production builder lifecycle without owning resources."""
