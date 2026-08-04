"""SQLite durability boundary for agent control and the synchronized watch view."""

from __future__ import annotations

import json
import sqlite3
import threading
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator


LATEST_SCHEMA_VERSION = 4
LEGACY_DEVICE_ID = "legacy-cores3"

SCHEMA_LATEST = """
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS conversations (
    conversation_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    started_at_ms INTEGER NOT NULL,
    ended_at_ms INTEGER
);
CREATE TABLE IF NOT EXISTS conversation_summaries (
    conversation_id TEXT NOT NULL REFERENCES conversations(conversation_id),
    device_id TEXT NOT NULL,
    sequence INTEGER NOT NULL,
    summary TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY (conversation_id, sequence)
);
CREATE TABLE IF NOT EXISTS jobs (
    job_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
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
    device_id TEXT NOT NULL,
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
    device_id TEXT NOT NULL,
    question_id TEXT NOT NULL,
    prompt TEXT NOT NULL,
    answer_schema_json TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(job_id, question_id)
);
CREATE UNIQUE INDEX IF NOT EXISTS one_focused_question_per_device
ON job_questions(device_id, status) WHERE status = 'focused';
CREATE TABLE IF NOT EXISTS job_answers (
    job_id TEXT NOT NULL,
    device_id TEXT NOT NULL,
    question_id TEXT NOT NULL,
    source_utterance_id TEXT NOT NULL,
    answer_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(job_id, question_id),
    FOREIGN KEY(job_id, question_id) REFERENCES job_questions(job_id, question_id),
    UNIQUE(device_id, source_utterance_id)
);
CREATE TABLE IF NOT EXISTS worker_leases (
    job_id TEXT PRIMARY KEY REFERENCES jobs(job_id),
    device_id TEXT NOT NULL,
    owner TEXT NOT NULL,
    expires_at_ms INTEGER NOT NULL,
    heartbeat_at_ms INTEGER NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS attention_deliveries (
    event_id TEXT NOT NULL REFERENCES job_events(event_id),
    device_id TEXT NOT NULL,
    channel TEXT NOT NULL,
    state TEXT NOT NULL,
    delivered_at_ms INTEGER,
    PRIMARY KEY(event_id, channel)
);
CREATE TABLE IF NOT EXISTS capability_invocations (
    device_id TEXT NOT NULL,
    idempotency_key TEXT NOT NULL,
    capability_id TEXT NOT NULL,
    input_json TEXT NOT NULL,
    result_json TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    PRIMARY KEY(device_id, idempotency_key)
);
CREATE TABLE IF NOT EXISTS watch_replicas (
    device_id TEXT PRIMARY KEY,
    revision INTEGER NOT NULL,
    snapshot_json TEXT NOT NULL,
    synchronized_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS nutrition_entries (
    entry_id TEXT PRIMARY KEY,
    device_id TEXT NOT NULL,
    description TEXT NOT NULL,
    quantity REAL NOT NULL,
    unit TEXT NOT NULL,
    provisional INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS codex_sessions (
    job_id TEXT PRIMARY KEY REFERENCES jobs(job_id),
    device_id TEXT NOT NULL,
    thread_id TEXT,
    turn_id TEXT,
    workspace_path TEXT NOT NULL,
    pending_question_json TEXT,
    stable_summary TEXT NOT NULL DEFAULT '',
    artifact_json TEXT,
    codex_version TEXT NOT NULL,
    stage TEXT NOT NULL DEFAULT 'eliciting_layout',
    updated_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS jobs_by_device ON jobs(device_id, created_at_ms);
CREATE INDEX IF NOT EXISTS events_by_device ON job_events(device_id, created_at_ms);
CREATE INDEX IF NOT EXISTS conversations_by_device
ON conversations(device_id, started_at_ms);
CREATE INDEX IF NOT EXISTS codex_sessions_by_device
ON codex_sessions(device_id, updated_at_ms);
PRAGMA user_version=4;
"""


class Store:
    def __init__(self, path: Path | str) -> None:
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path, check_same_thread=False)
        self.connection.row_factory = sqlite3.Row
        self._lock = threading.RLock()
        with self._lock:
            self._migrate()

    def _migrate(self) -> None:
        version = int(self.connection.execute("PRAGMA user_version").fetchone()[0])
        if version > LATEST_SCHEMA_VERSION:
            raise RuntimeError(
                f"database schema {version} is newer than supported "
                f"{LATEST_SCHEMA_VERSION}"
            )
        has_legacy_schema = self.connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='jobs'"
        ).fetchone() is not None
        if not has_legacy_schema:
            self.connection.executescript(SCHEMA_LATEST)
            return
        if version < 2:
            self._migrate_legacy_to_v2()
            version = 2
        if version < 3:
            self._migrate_v2_to_v3()
            version = 3
        if version < 4:
            self._migrate_v3_to_v4()

    def _columns(self, table: str) -> set[str]:
        return {
            str(row["name"])
            for row in self.connection.execute(f"PRAGMA table_info({table})")
        }

    def _migrate_legacy_to_v2(self) -> None:
        self.connection.commit()
        self.connection.execute("PRAGMA foreign_keys=OFF")
        try:
            self.connection.execute("BEGIN IMMEDIATE")
            for table in (
                "conversations", "conversation_summaries", "jobs", "job_events",
                "job_questions", "worker_leases", "attention_deliveries",
                "nutrition_entries",
            ):
                if "device_id" not in self._columns(table):
                    self.connection.execute(
                        f"ALTER TABLE {table} ADD COLUMN device_id TEXT NOT NULL "
                        f"DEFAULT '{LEGACY_DEVICE_ID}'"
                    )

            self.connection.execute("DROP INDEX IF EXISTS one_focused_question")
            self.connection.execute(
                "CREATE UNIQUE INDEX IF NOT EXISTS one_focused_question_per_device "
                "ON job_questions(device_id,status) WHERE status='focused'"
            )

            invocation_columns = self._columns("capability_invocations")
            self.connection.execute(
                "ALTER TABLE capability_invocations RENAME TO capability_invocations_v1"
            )
            self.connection.execute(
                """CREATE TABLE capability_invocations (
                    device_id TEXT NOT NULL,
                    idempotency_key TEXT NOT NULL,
                    capability_id TEXT NOT NULL,
                    input_json TEXT NOT NULL,
                    result_json TEXT NOT NULL,
                    created_at_ms INTEGER NOT NULL,
                    PRIMARY KEY(device_id,idempotency_key))"""
            )
            device_expression = (
                "device_id" if "device_id" in invocation_columns
                else f"'{LEGACY_DEVICE_ID}'"
            )
            self.connection.execute(
                "INSERT INTO capability_invocations "
                "SELECT " + device_expression + ",idempotency_key,capability_id,"
                "input_json,result_json,created_at_ms FROM capability_invocations_v1"
            )
            self.connection.execute("DROP TABLE capability_invocations_v1")

            answer_columns = self._columns("job_answers")
            self.connection.execute("ALTER TABLE job_answers RENAME TO job_answers_v1")
            self.connection.execute(
                """CREATE TABLE job_answers (
                    job_id TEXT NOT NULL,
                    device_id TEXT NOT NULL,
                    question_id TEXT NOT NULL,
                    source_utterance_id TEXT NOT NULL,
                    answer_json TEXT NOT NULL,
                    created_at_ms INTEGER NOT NULL,
                    PRIMARY KEY(job_id,question_id),
                    FOREIGN KEY(job_id,question_id)
                        REFERENCES job_questions(job_id,question_id),
                    UNIQUE(device_id,source_utterance_id))"""
            )
            answer_device = (
                "device_id" if "device_id" in answer_columns
                else f"'{LEGACY_DEVICE_ID}'"
            )
            self.connection.execute(
                "INSERT INTO job_answers SELECT job_id," + answer_device +
                ",question_id,source_utterance_id,answer_json,created_at_ms "
                "FROM job_answers_v1"
            )
            self.connection.execute("DROP TABLE job_answers_v1")
            self.connection.execute(
                "CREATE INDEX IF NOT EXISTS jobs_by_device "
                "ON jobs(device_id,created_at_ms)"
            )
            self.connection.execute(
                "CREATE INDEX IF NOT EXISTS events_by_device "
                "ON job_events(device_id,created_at_ms)"
            )
            self.connection.execute(
                "CREATE INDEX IF NOT EXISTS conversations_by_device "
                "ON conversations(device_id,started_at_ms)"
            )
            self.connection.execute("PRAGMA user_version=2")
            self.connection.commit()
        except BaseException:
            self.connection.rollback()
            raise
        finally:
            self.connection.execute("PRAGMA foreign_keys=ON")

    def _migrate_v2_to_v3(self) -> None:
        with self.transaction() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS codex_sessions (
                    job_id TEXT PRIMARY KEY REFERENCES jobs(job_id),
                    device_id TEXT NOT NULL,
                    thread_id TEXT,
                    turn_id TEXT,
                    workspace_path TEXT NOT NULL,
                    pending_question_json TEXT,
                    stable_summary TEXT NOT NULL DEFAULT '',
                    artifact_json TEXT,
                    codex_version TEXT NOT NULL,
                    updated_at_ms INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS codex_sessions_by_device
                ON codex_sessions(device_id, updated_at_ms);
                PRAGMA user_version=3;
                """
            )

    def _migrate_v3_to_v4(self) -> None:
        with self.transaction() as connection:
            if "stage" not in self._columns("codex_sessions"):
                connection.execute(
                    "ALTER TABLE codex_sessions ADD COLUMN stage TEXT NOT NULL "
                    "DEFAULT 'eliciting_layout'"
                )
            connection.execute("PRAGMA user_version=4")

    def relink_legacy_device(self, device_id: str) -> None:
        """Assign pre-v2 CoreS3 rows to its stable MAC-derived identity."""
        with self.transaction() as connection:
            for table in (
                "conversations", "conversation_summaries", "jobs", "job_events",
                "job_questions", "job_answers", "worker_leases",
                "attention_deliveries", "capability_invocations",
                "nutrition_entries", "codex_sessions",
            ):
                connection.execute(
                    f"UPDATE {table} SET device_id=? WHERE device_id=?",
                    (device_id, LEGACY_DEVICE_ID),
                )
            if connection.execute(
                "SELECT 1 FROM watch_replicas WHERE device_id=?", (device_id,)
            ).fetchone() is None:
                connection.execute(
                    "UPDATE watch_replicas SET device_id=? "
                    "WHERE device_id IN (?, 'cores3-se')",
                    (device_id, LEGACY_DEVICE_ID),
                )

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

    def fetch_one(self, query: str, parameters: tuple[object, ...] = ()) -> Any:
        with self._lock:
            return self.connection.execute(query, parameters).fetchone()

    def fetch_all(self, query: str, parameters: tuple[object, ...] = ()) -> list[Any]:
        with self._lock:
            return self.connection.execute(query, parameters).fetchall()

    @staticmethod
    def encode(value: object) -> str:
        return json.dumps(value, sort_keys=True, separators=(",", ":"))

    @staticmethod
    def decode(value: str) -> object:
        return json.loads(value)
