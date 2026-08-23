"""Append-only durable jobs, typed questions, leases, and recovery."""

from __future__ import annotations

import json
import re
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from .storage import Store


EVENT_STATE = {
    "accepted": "queued",
    "started": "running",
    "progress": None,
    "needs_input": "needs_input",
    "input_received": "running",
    "ready_for_review": "ready_for_review",
    "completed": "completed",
    "failed": "failed",
    "cancel_requested": None,
    "cancelled": "cancelled",
}
TERMINAL = {"completed", "failed", "cancelled"}

TASK_PRESENTATION = {
    "codex_app_build": {
        "kind": "app_build",
        "title": "BUILDING APP",
        "icon": "app_builder",
        "color": "#7241FF",
        "detail_label": "REQUEST",
        "stages": ["PLAN", "DESIGN", "BUILD", "VERIFY"],
    },
    "fake_app_build": {
        "kind": "app_build",
        "title": "BUILDING APP",
        "icon": "app_builder",
        "color": "#7241FF",
        "detail_label": "REQUEST",
        "stages": ["PLAN", "DESIGN", "BUILD", "VERIFY"],
    },
    "research_report": {
        "kind": "research_report",
        "title": "RESEARCH REPORT",
        "icon": "research",
        "color": "#20BFF4",
        "detail_label": "TOPIC",
        "stages": ["BRIEF", "RESEARCH", "DRAFT", "REVIEW"],
    },
    "presentation_delivery": {
        "kind": "presentation_delivery",
        "title": "SLIDE DECK",
        "icon": "presentation",
        "color": "#B9FF24",
        "detail_label": "DELIVERY",
        "stages": ["BRIEF", "CREATE", "REVIEW", "SEND"],
    },
}


def _bounded_text(value: object, maximum: int) -> str:
    return " ".join(str(value or "").split())[:maximum]


def _elapsed_text(start_ms: int, now_ms: int) -> str:
    seconds = max(0, (now_ms - start_ms) // 1000)
    minutes, seconds = divmod(seconds, 60)
    return f"{min(minutes, 99)}:{seconds:02d}"


def iso_time(milliseconds: int) -> str:
    return datetime.fromtimestamp(milliseconds / 1000, tz=UTC).isoformat().replace("+00:00", "Z")


@dataclass(frozen=True)
class JobEvent:
    event_id: str
    job_id: str
    sequence: int
    kind: str
    created_at_ms: int
    summary: str
    payload: dict[str, Any]
    producer: str

    def document(self) -> dict[str, Any]:
        return {
            "event_id": self.event_id,
            "job_id": self.job_id,
            "sequence": self.sequence,
            "kind": self.kind,
            "created_at": iso_time(self.created_at_ms),
            "summary": self.summary,
            "payload": self.payload,
            "producer": self.producer,
        }


class JobManager:
    def __init__(self, store: Store, device_id: str = "cores3-se") -> None:
        self.store = store
        self.device_id = device_id

    def create(self, kind: str, request: dict[str, Any], now_ms: int) -> str:
        job_id = f"job_{uuid.uuid4().hex}"
        with self.store.transaction() as connection:
            connection.execute(
                "INSERT INTO jobs(job_id,device_id,kind,state,request_json,created_at_ms,updated_at_ms) VALUES(?,?,?,?,?,?,?)",
                (job_id, self.device_id, kind, "new", Store.encode(request), now_ms, now_ms),
            )
        self.append(job_id, "accepted", "Background job accepted.", request, "foreground", now_ms)
        return job_id

    def append(
        self,
        job_id: str,
        kind: str,
        summary: str,
        payload: dict[str, Any],
        producer: str,
        now_ms: int,
        *,
        event_id: str | None = None,
    ) -> JobEvent:
        if kind not in EVENT_STATE:
            raise ValueError(f"unsupported job event: {kind}")
        event_id = event_id or f"evt_{uuid.uuid4().hex}"
        with self.store.transaction() as connection:
            duplicate = connection.execute(
                "SELECT * FROM job_events WHERE device_id=? AND event_id=?",
                (self.device_id, event_id),
            ).fetchone()
            if duplicate is not None:
                return self._event(duplicate)
            job = connection.execute(
                "SELECT * FROM jobs WHERE device_id=? AND job_id=?",
                (self.device_id, job_id),
            ).fetchone()
            if job is None:
                raise KeyError(job_id)
            if job["state"] in TERMINAL:
                raise ValueError(f"job {job_id} is terminal")
            sequence = int(job["next_sequence"])
            connection.execute(
                "INSERT INTO job_events "
                "(event_id,job_id,device_id,sequence,kind,created_at_ms,summary,payload_json,producer) "
                "VALUES(?,?,?,?,?,?,?,?,?)",
                (event_id, job_id, self.device_id, sequence, kind, now_ms, summary, Store.encode(payload), producer),
            )
            state = EVENT_STATE[kind] or job["state"]
            connection.execute(
                "UPDATE jobs SET state=?,updated_at_ms=?,next_sequence=? "
                "WHERE device_id=? AND job_id=?",
                (state, now_ms, sequence + 1, self.device_id, job_id),
            )
            if kind == "needs_input":
                question = payload.get("question")
                if not isinstance(question, dict):
                    raise ValueError("needs_input requires question payload")
                connection.execute(
                    "INSERT INTO job_questions "
                    "(job_id,device_id,question_id,prompt,answer_schema_json,status,created_at_ms) "
                    "VALUES(?,?,?,?,?,?,?) ON CONFLICT(job_id,question_id) DO NOTHING",
                    (
                        job_id,
                        self.device_id,
                        str(question["id"]),
                        str(question["prompt"]),
                        Store.encode(question["answer_schema"]),
                        "open",
                        now_ms,
                    ),
                )
        return JobEvent(event_id, job_id, sequence, kind, now_ms, summary, payload, producer)

    def answer(
        self,
        job_id: str,
        question_id: str,
        answer: object,
        source_utterance_id: str,
        now_ms: int,
    ) -> bool:
        with self.store.transaction() as connection:
            prior = connection.execute(
                "SELECT 1 FROM job_answers WHERE device_id=? AND "
                "(source_utterance_id=? OR (job_id=? AND question_id=?))",
                (self.device_id, source_utterance_id, job_id, question_id),
            ).fetchone()
            if prior is not None:
                return False
            question = connection.execute(
                "SELECT * FROM job_questions WHERE device_id=? AND job_id=? AND question_id=?",
                (self.device_id, job_id, question_id),
            ).fetchone()
            if question is None or question["status"] not in {"open", "focused"}:
                raise ValueError("question is not answerable")
            schema = json.loads(question["answer_schema_json"])
            choices = schema.get("enum")
            if choices is not None and answer not in choices:
                raise ValueError(f"answer must be one of {choices}")
            connection.execute(
                "INSERT INTO job_answers "
                "(job_id,device_id,question_id,source_utterance_id,answer_json,created_at_ms) "
                "VALUES(?,?,?,?,?,?)",
                (job_id, self.device_id, question_id, source_utterance_id, Store.encode(answer), now_ms),
            )
            connection.execute(
                "UPDATE job_questions SET status='answered' "
                "WHERE device_id=? AND job_id=? AND question_id=?",
                (self.device_id, job_id, question_id),
            )
        self.append(
            job_id,
            "input_received",
            "Builder input received.",
            {"question_id": question_id, "answer": answer},
            "foreground",
            now_ms,
        )
        return True

    def focus(self, job_id: str, question_id: str) -> None:
        with self.store.transaction() as connection:
            connection.execute(
                "UPDATE job_questions SET status='open' "
                "WHERE device_id=? AND status='focused'", (self.device_id,)
            )
            changed = connection.execute(
                "UPDATE job_questions SET status='focused' "
                "WHERE device_id=? AND job_id=? AND question_id=? AND status='open'",
                (self.device_id, job_id, question_id),
            ).rowcount
            if changed != 1:
                raise ValueError("question cannot receive focus")

    def focused(self) -> dict[str, Any] | None:
        row = self.store.fetch_one(
            "SELECT * FROM job_questions WHERE device_id=? AND status='focused'",
            (self.device_id,),
        )
        return self._question(row) if row else None

    def open_questions(self) -> list[dict[str, Any]]:
        rows = self.store.fetch_all(
            """
            SELECT q.* FROM job_questions q JOIN jobs j ON j.job_id=q.job_id
            WHERE q.device_id=? AND j.device_id=? AND q.status IN ('open','focused')
            ORDER BY q.created_at_ms,j.created_at_ms,q.job_id
            """, (self.device_id, self.device_id)
        )
        return [self._question(row) for row in rows]

    def job(self, job_id: str) -> dict[str, Any]:
        row = self.store.fetch_one(
            "SELECT * FROM jobs WHERE device_id=? AND job_id=?",
            (self.device_id, job_id),
        )
        if row is None:
            raise KeyError(job_id)
        return dict(row)

    def events(self, job_id: str) -> list[JobEvent]:
        rows = self.store.fetch_all(
            "SELECT * FROM job_events WHERE device_id=? AND job_id=? ORDER BY sequence",
            (self.device_id, job_id),
        )
        return [self._event(row) for row in rows]

    def task_snapshots(
        self,
        now_ms: int,
        *,
        active_only: bool = True,
        limit: int = 3,
    ) -> list[dict[str, Any]]:
        """Project the durable ledger into a bounded, device-safe task feed."""

        if limit <= 0:
            return []
        state_clause = (
            "AND state NOT IN ('ready_for_review','completed','failed','cancelled')"
            if active_only else ""
        )
        rows = self.store.fetch_all(
            "SELECT * FROM jobs WHERE device_id=? " + state_clause +
            " ORDER BY updated_at_ms DESC,created_at_ms DESC,job_id LIMIT ?",
            (self.device_id, limit),
        )
        return [self._task_snapshot(row, now_ms) for row in rows]

    def task_status(
        self,
        now_ms: int,
        query: str | None = None,
        *,
        limit: int = 8,
    ) -> list[dict[str, Any]]:
        """Return recent task status, optionally narrowed by natural text."""

        snapshots = self.task_snapshots(
            now_ms, active_only=False, limit=max(limit, 24)
        )
        normalized = " ".join((query or "").casefold().split())
        if normalized:
            generic = {
                "agent", "agents", "are", "current", "doing", "going",
                "how", "progress", "status", "task", "tasks", "the", "what",
            }
            terms = {
                term for term in re.findall(r"[a-z0-9]+", normalized)
                if len(term) > 2 and term not in generic
            }
            if terms:
                snapshots = [
                    snapshot for snapshot in snapshots
                    if terms.intersection(
                        set(re.findall(
                            r"[a-z0-9]+",
                            " ".join(
                                str(snapshot.get(key, ""))
                                for key in ("job_id", "kind", "title", "detail", "summary")
                            ).casefold(),
                        ))
                    )
                ]
        return snapshots[:limit]

    def _task_snapshot(self, row: Any, now_ms: int) -> dict[str, Any]:
        request = json.loads(row["request_json"])
        presentation = TASK_PRESENTATION.get(str(row["kind"]), {
            "kind": "background_work",
            "title": "BACKGROUND TASK",
            "icon": "monitoring",
            "color": "#B9FF24",
            "detail_label": "REQUEST",
            "stages": ["BRIEF", "WORK", "REVIEW", "DONE"],
        })
        events = self.events(str(row["job_id"]))
        latest = events[-1] if events else None
        progress_event = next(
            (event for event in reversed(events) if event.kind == "progress"),
            None,
        )
        payload = progress_event.payload if progress_event is not None else {}
        stages = list(presentation["stages"])
        stage_index = self._stage_index(
            str(row["kind"]), str(row["state"]), payload, stages
        )
        progress = payload.get("progress")
        if not isinstance(progress, int):
            progress = min(95, round(stage_index * 100 / max(1, len(stages) - 1)))
        if row["state"] in {"completed", "ready_for_review"}:
            progress = 100
        detail = (
            request.get("recipient")
            if presentation["kind"] == "presentation_delivery"
            else request.get("topic") or request.get("brief")
        )
        summary = latest.summary if latest is not None else "Background work accepted."
        return {
            "job_id": str(row["job_id"]),
            "kind": presentation["kind"],
            "title": presentation["title"],
            "status": self._status_text(str(row["state"]), payload, summary),
            "state": str(row["state"]),
            "summary": _bounded_text(summary, 160),
            "elapsed": _elapsed_text(int(row["created_at_ms"]), now_ms),
            "progress": max(0, min(int(progress), 100)),
            "detail_label": presentation["detail_label"],
            "detail": _bounded_text(detail or "IN PROGRESS", 48).upper(),
            "stages": stages,
            "active_stage": min(stage_index, len(stages) - 1),
            "completed_stages": min(stage_index, len(stages)),
            "color": presentation["color"],
            "icon": presentation["icon"],
            "updated_at_ms": int(row["updated_at_ms"]),
        }

    @staticmethod
    def _stage_index(
        job_kind: str,
        state: str,
        payload: dict[str, Any],
        stages: list[str],
    ) -> int:
        if state in {"completed", "ready_for_review"}:
            return len(stages) - 1
        explicit = payload.get("stage_index")
        if isinstance(explicit, int):
            return max(0, min(explicit, len(stages) - 1))
        stage = str(payload.get("stage", "")).casefold()
        if job_kind in {"codex_app_build", "fake_app_build"}:
            if stage in {"design", "designing"}:
                return 1
            if stage in {"implementation", "implementing", "build"}:
                return 2
            if stage in {"verification", "repair", "verifying", "repairing"}:
                return 3
            if stage in {"packaging", "install"}:
                return 3
            return 0
        return 1 if state in {"running", "needs_input"} else 0

    @staticmethod
    def _status_text(state: str, payload: dict[str, Any], summary: str) -> str:
        if state == "needs_input":
            return "NEEDS INPUT"
        if state == "ready_for_review":
            return "READY TO INSTALL"
        if state == "completed":
            return "COMPLETED"
        if state == "failed":
            return "FAILED"
        if state == "cancelled":
            return "CANCELLED"
        value = payload.get("status") or payload.get("stage")
        if value:
            return _bounded_text(value, 24).replace("_", " ").upper()
        if state == "queued":
            return "QUEUED"
        lowered = summary.casefold()
        if "research" in lowered:
            return "RESEARCHING"
        if "slide" in lowered or "deck" in lowered:
            return "CREATING SLIDES"
        return "WORKING"

    def rebuild_state(self, job_id: str) -> str:
        state = "new"
        for event in self.events(job_id):
            state = EVENT_STATE[event.kind] or state
        return state

    def recover_expired(self, now_ms: int) -> list[str]:
        with self.store.transaction() as connection:
            rows = connection.execute(
                "SELECT job_id FROM jobs WHERE device_id=? AND state='running' "
                "AND lease_expires_ms IS NOT NULL AND lease_expires_ms<?",
                (self.device_id, now_ms),
            ).fetchall()
            ids = [str(row["job_id"]) for row in rows]
            for job_id in ids:
                connection.execute(
                    "UPDATE jobs SET state='queued',lease_owner=NULL,lease_expires_ms=NULL,updated_at_ms=? "
                    "WHERE device_id=? AND job_id=?",
                    (now_ms, self.device_id, job_id),
                )
                connection.execute(
                    "DELETE FROM worker_leases WHERE device_id=? AND job_id=?",
                    (self.device_id, job_id),
                )
        return ids

    @staticmethod
    def _event(row: Any) -> JobEvent:
        return JobEvent(
            row["event_id"], row["job_id"], row["sequence"], row["kind"],
            row["created_at_ms"], row["summary"], json.loads(row["payload_json"]), row["producer"],
        )

    @staticmethod
    def _question(row: Any) -> dict[str, Any]:
        return {
            "job_id": row["job_id"], "question_id": row["question_id"],
            "prompt": row["prompt"], "answer_schema": json.loads(row["answer_schema_json"]),
            "status": row["status"], "created_at_ms": row["created_at_ms"],
        }
