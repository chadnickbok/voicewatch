"""SQLite durability boundary for agent control and the synchronized watch view."""

from __future__ import annotations

import json
import sqlite3
import threading
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator


MIGRATION = """
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS conversations (
    conversation_id TEXT PRIMARY KEY,
    started_at_ms INTEGER NOT NULL,
    ended_at_ms INTEGER
);
CREATE TABLE IF NOT EXISTS conversation_summaries (
    conversation_id TEXT NOT NULL REFERENCES conversations(conversation_id),
    sequence INTEGER NOT NULL,
    summary TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY (conversation_id, sequence)
);
CREATE TABLE IF NOT EXISTS jobs (
    job_id TEXT PRIMARY KEY,
    kind TEXT NOT NULL,
    state TEXT NOT NULL,
    request_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    next_sequence INTEGER NOT NULL DEFAULT 1,
    lease_owner TEXT,
    lease_expires_ms INTEGER
);
CREATE TABLE IF NOT EXISTS job_events (
    event_id TEXT PRIMARY KEY,
    job_id TEXT NOT NULL REFERENCES jobs(job_id),
    sequence INTEGER NOT NULL,
    kind TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    summary TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    producer TEXT NOT NULL,
    UNIQUE(job_id, sequence)
);
CREATE TABLE IF NOT EXISTS job_questions (
    job_id TEXT NOT NULL REFERENCES jobs(job_id),
    question_id TEXT NOT NULL,
    prompt TEXT NOT NULL,
    answer_schema_json TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(job_id, question_id)
);
CREATE UNIQUE INDEX IF NOT EXISTS one_focused_question
ON job_questions(status) WHERE status = 'focused';
CREATE TABLE IF NOT EXISTS job_answers (
    job_id TEXT NOT NULL,
    question_id TEXT NOT NULL,
    source_utterance_id TEXT NOT NULL UNIQUE,
    answer_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(job_id, question_id),
    FOREIGN KEY(job_id, question_id) REFERENCES job_questions(job_id, question_id)
);
CREATE TABLE IF NOT EXISTS worker_leases (
    job_id TEXT PRIMARY KEY REFERENCES jobs(job_id),
    owner TEXT NOT NULL,
    expires_at_ms INTEGER NOT NULL,
    heartbeat_at_ms INTEGER NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS attention_deliveries (
    event_id TEXT NOT NULL REFERENCES job_events(event_id),
    channel TEXT NOT NULL,
    state TEXT NOT NULL,
    delivered_at_ms INTEGER,
    PRIMARY KEY(event_id, channel)
);
CREATE TABLE IF NOT EXISTS capability_invocations (
    idempotency_key TEXT PRIMARY KEY,
    capability_id TEXT NOT NULL,
    input_json TEXT NOT NULL,
    result_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS watch_replicas (
    device_id TEXT PRIMARY KEY,
    revision INTEGER NOT NULL,
    snapshot_json TEXT NOT NULL,
    synchronized_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS nutrition_entries (
    entry_id TEXT PRIMARY KEY,
    description TEXT NOT NULL,
    quantity REAL NOT NULL,
    unit TEXT NOT NULL,
    provisional INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL
);
"""


class Store:
    def __init__(self, path: Path | str) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path, check_same_thread=False)
        self.connection.row_factory = sqlite3.Row
        self._lock = threading.RLock()
        with self._lock:
            self.connection.executescript(MIGRATION)

    @contextmanager
    def transaction(self) -> Iterator[sqlite3.Connection]:
        with self._lock:
            try:
                self.connection.execute("BEGIN IMMEDIATE")
                yield self.connection
                self.connection.commit()
            except BaseException:
                self.connection.rollback()
                raise

    def close(self) -> None:
        with self._lock:
            self.connection.close()

    @staticmethod
    def encode(value: object) -> str:
        return json.dumps(value, sort_keys=True, separators=(",", ":"))

    @staticmethod
    def decode(value: str) -> object:
        return json.loads(value)
