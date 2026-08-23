"""Deterministic concurrent report and presentation worker for the E2E lane."""

from __future__ import annotations

from .jobs import JobManager


class FakeWorkBuilder:
    STAGE_MS = 5_000
    SUPPORTED = {"research_report", "presentation_delivery"}

    def __init__(self, jobs: JobManager) -> None:
        self.jobs = jobs

    def start_work(
        self,
        kind: str,
        brief: str,
        now_ms: int,
        *,
        recipient: str | None = None,
    ) -> str:
        if kind not in self.SUPPORTED:
            raise ValueError(f"unsupported background work kind: {kind}")
        request: dict[str, object] = {
            "brief": " ".join(brief.split())[:500],
        }
        if kind == "research_report":
            request["topic"] = request["brief"]
        if recipient:
            request["recipient"] = " ".join(recipient.split())[:254]
        job_id = self.jobs.create(kind, request, now_ms)
        status = "RESEARCHING" if kind == "research_report" else "BUILDING DECK"
        self.jobs.append(
            job_id,
            "started",
            "Research started in the background."
            if kind == "research_report"
            else "The slide deck workflow started in the background.",
            {"stage_index": 1, "progress": 10, "status": status},
            "fake-work-worker",
            now_ms,
        )
        return job_id

    def tick(self, now_ms: int) -> list[str]:
        changed: list[str] = []
        rows = self.jobs.store.fetch_all(
            "SELECT * FROM jobs WHERE device_id=? "
            "AND kind IN ('research_report','presentation_delivery') "
            "AND state NOT IN ('completed','failed','cancelled') "
            "ORDER BY created_at_ms,job_id",
            (self.jobs.device_id,),
        )
        for row in rows:
            job_id = str(row["job_id"])
            age = now_ms - int(row["created_at_ms"])
            events = self.jobs.events(job_id)
            progress_count = sum(event.kind == "progress" for event in events)
            kind = str(row["kind"])
            if age >= self.STAGE_MS and progress_count == 0:
                self.jobs.append(
                    job_id,
                    "progress",
                    "The first pass is complete.",
                    {
                        "stage_index": 2,
                        "progress": 55,
                        "status": "DRAFTING"
                        if kind == "research_report" else "REVIEWING DECK",
                    },
                    "fake-work-worker",
                    now_ms,
                )
                changed.append(job_id)
            elif age >= self.STAGE_MS * 2 and progress_count == 1:
                self.jobs.append(
                    job_id,
                    "progress",
                    "The output is in final review.",
                    {
                        "stage_index": 3,
                        "progress": 85,
                        "status": "REVIEWING"
                        if kind == "research_report" else "SENDING",
                    },
                    "fake-work-worker",
                    now_ms,
                )
                changed.append(job_id)
            elif age >= self.STAGE_MS * 3 and progress_count == 2:
                request = self.jobs.store.decode(row["request_json"])
                self.jobs.append(
                    job_id,
                    "completed",
                    "Your research report is ready."
                    if kind == "research_report"
                    else "The slide deck was created and emailed.",
                    {
                        "artifact": "report.md"
                        if kind == "research_report" else "presentation.pptx",
                        "recipient": request.get("recipient"),
                    },
                    "fake-work-worker",
                    now_ms,
                )
                changed.append(job_id)
        return changed

    def close(self) -> None:
        return None
