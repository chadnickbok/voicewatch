"""Deterministic focus and delivery policy; the LLM never decides interruption."""

from __future__ import annotations

from dataclasses import dataclass

from .jobs import JobEvent, JobManager
from .storage import Store


@dataclass(frozen=True)
class AttentionAction:
    kind: str
    text: str
    job_id: str
    question_id: str | None = None


class AttentionBroker:
    def __init__(self, store: Store, jobs: JobManager) -> None:
        self.store = store
        self.jobs = jobs

    def observe(self, event: JobEvent, now_ms: int) -> None:
        channels = ["display"]
        if event.kind == "needs_input":
            channels.append("haptic")
        elif event.kind in {"completed", "ready_for_review"}:
            channels.extend(["haptic", "spoken"])
        elif event.kind == "failed":
            channels.append("haptic")
        with self.store.transaction() as connection:
            for channel in channels:
                connection.execute(
                    "INSERT INTO attention_deliveries(event_id,channel,state) VALUES(?,?,?) ON CONFLICT DO NOTHING",
                    (event.event_id, channel, "pending"),
                )
            connection.execute(
                "UPDATE attention_deliveries SET state='delivered',delivered_at_ms=? WHERE event_id=? AND channel='display' AND state='pending'",
                (now_ms, event.event_id),
            )

    def natural_pause(self, now_ms: int) -> AttentionAction | None:
        focused = self.jobs.focused()
        if focused is not None:
            return None
        questions = self.jobs.open_questions()
        if questions:
            focused = questions[0]
            if focused["status"] == "open":
                self.jobs.focus(focused["job_id"], focused["question_id"])
            event = self._question_event(focused["job_id"], focused["question_id"])
            self._deliver(event.event_id, "haptic", now_ms)
            return AttentionAction("question", focused["prompt"], focused["job_id"], focused["question_id"])

        row = self.store.connection.execute(
            """
            SELECT e.* FROM attention_deliveries d
            JOIN job_events e ON e.event_id=d.event_id
            WHERE d.channel='spoken' AND d.state='pending'
            ORDER BY e.created_at_ms,e.job_id,e.sequence LIMIT 1
            """
        ).fetchone()
        if row is None:
            return None
        self._deliver(row["event_id"], "spoken", now_ms)
        return AttentionAction("announcement", row["summary"], row["job_id"])

    def answer_focused(self, answer: object, utterance_id: str, now_ms: int) -> bool:
        focused = self.jobs.focused()
        if focused is None:
            return False
        return self.jobs.answer(
            focused["job_id"], focused["question_id"], answer, utterance_id, now_ms
        )

    def background_snapshot(self) -> dict[str, object]:
        running = self.store.connection.execute(
            "SELECT COUNT(*) FROM jobs WHERE state NOT IN ('completed','failed','cancelled')"
        ).fetchone()[0]
        completion = self.store.connection.execute(
            "SELECT COUNT(*) FROM attention_deliveries WHERE channel='spoken' AND state='pending'"
        ).fetchone()[0]
        focused = self.jobs.focused()
        return {
            "running_count": int(running),
            "focused_question": focused,
            "completion_pending": int(completion),
        }

    def _deliver(self, event_id: str, channel: str, now_ms: int) -> None:
        with self.store.transaction() as connection:
            connection.execute(
                "UPDATE attention_deliveries SET state='delivered',delivered_at_ms=? WHERE event_id=? AND channel=? AND state='pending'",
                (now_ms, event_id, channel),
            )

    def _question_event(self, job_id: str, question_id: str) -> JobEvent:
        return next(
            event for event in self.jobs.events(job_id)
            if event.kind == "needs_input" and event.payload["question"]["id"] == question_id
        )
