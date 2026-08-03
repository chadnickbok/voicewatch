"""Append-only durable jobs, typed questions, leases, and recovery."""

from __future__ import annotations

import json
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
    def __init__(self, store: Store) -> None:
        self.store = store

    def create(self, kind: str, request: dict[str, Any], now_ms: int) -> str:
        job_id = f"job_{uuid.uuid4().hex}"
        with self.store.transaction() as connection:
            connection.execute(
                "INSERT INTO jobs(job_id,kind,state,request_json,created_at_ms,updated_at_ms) VALUES(?,?,?,?,?,?)",
                (job_id, kind, "new", Store.encode(request), now_ms, now_ms),
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
                "SELECT * FROM job_events WHERE event_id=?", (event_id,)
            ).fetchone()
            if duplicate is not None:
                return self._event(duplicate)
            job = connection.execute("SELECT * FROM jobs WHERE job_id=?", (job_id,)).fetchone()
            if job is None:
                raise KeyError(job_id)
            if job["state"] in TERMINAL:
                raise ValueError(f"job {job_id} is terminal")
            sequence = int(job["next_sequence"])
            connection.execute(
                "INSERT INTO job_events VALUES(?,?,?,?,?,?,?,?)",
                (event_id, job_id, sequence, kind, now_ms, summary, Store.encode(payload), producer),
            )
            state = EVENT_STATE[kind] or job["state"]
            connection.execute(
                "UPDATE jobs SET state=?,updated_at_ms=?,next_sequence=? WHERE job_id=?",
                (state, now_ms, sequence + 1, job_id),
            )
            if kind == "needs_input":
                question = payload.get("question")
                if not isinstance(question, dict):
                    raise ValueError("needs_input requires question payload")
                connection.execute(
                    "INSERT INTO job_questions VALUES(?,?,?,?,?,?) ON CONFLICT(job_id,question_id) DO NOTHING",
                    (
                        job_id,
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
                "SELECT 1 FROM job_answers WHERE source_utterance_id=? OR (job_id=? AND question_id=?)",
                (source_utterance_id, job_id, question_id),
            ).fetchone()
            if prior is not None:
                return False
            question = connection.execute(
                "SELECT * FROM job_questions WHERE job_id=? AND question_id=?",
                (job_id, question_id),
            ).fetchone()
            if question is None or question["status"] not in {"open", "focused"}:
                raise ValueError("question is not answerable")
            schema = json.loads(question["answer_schema_json"])
            choices = schema.get("enum")
            if choices is not None and answer not in choices:
                raise ValueError(f"answer must be one of {choices}")
            connection.execute(
                "INSERT INTO job_answers VALUES(?,?,?,?,?)",
                (job_id, question_id, source_utterance_id, Store.encode(answer), now_ms),
            )
            connection.execute(
                "UPDATE job_questions SET status='answered' WHERE job_id=? AND question_id=?",
                (job_id, question_id),
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
            connection.execute("UPDATE job_questions SET status='open' WHERE status='focused'")
            changed = connection.execute(
                "UPDATE job_questions SET status='focused' WHERE job_id=? AND question_id=? AND status='open'",
                (job_id, question_id),
            ).rowcount
            if changed != 1:
                raise ValueError("question cannot receive focus")

    def focused(self) -> dict[str, Any] | None:
        row = self.store.connection.execute(
            "SELECT * FROM job_questions WHERE status='focused'"
        ).fetchone()
        return self._question(row) if row else None

    def open_questions(self) -> list[dict[str, Any]]:
        rows = self.store.connection.execute(
            """
            SELECT q.* FROM job_questions q JOIN jobs j ON j.job_id=q.job_id
            WHERE q.status IN ('open','focused')
            ORDER BY q.created_at_ms,j.created_at_ms,q.job_id
            """
        ).fetchall()
        return [self._question(row) for row in rows]

    def job(self, job_id: str) -> dict[str, Any]:
        row = self.store.connection.execute("SELECT * FROM jobs WHERE job_id=?", (job_id,)).fetchone()
        if row is None:
            raise KeyError(job_id)
        return dict(row)

    def events(self, job_id: str) -> list[JobEvent]:
        rows = self.store.connection.execute(
            "SELECT * FROM job_events WHERE job_id=? ORDER BY sequence", (job_id,)
        ).fetchall()
        return [self._event(row) for row in rows]

    def rebuild_state(self, job_id: str) -> str:
        state = "new"
        for event in self.events(job_id):
            state = EVENT_STATE[event.kind] or state
        return state

    def recover_expired(self, now_ms: int) -> list[str]:
        with self.store.transaction() as connection:
            rows = connection.execute(
                "SELECT job_id FROM jobs WHERE state='running' AND lease_expires_ms IS NOT NULL AND lease_expires_ms<?",
                (now_ms,),
            ).fetchall()
            ids = [str(row["job_id"]) for row in rows]
            for job_id in ids:
                connection.execute(
                    "UPDATE jobs SET state='queued',lease_owner=NULL,lease_expires_ms=NULL,updated_at_ms=? WHERE job_id=?",
                    (now_ms, job_id),
                )
                connection.execute("DELETE FROM worker_leases WHERE job_id=?", (job_id,))
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
