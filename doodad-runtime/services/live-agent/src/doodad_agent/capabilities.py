"""Typed, bounded watch capability kernel with revision and idempotency checks."""

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass
from typing import Any

from .storage import Store


class StaleWatchState(ValueError):
    pass


class AmbiguousReference(ValueError):
    pass


@dataclass(frozen=True)
class Capability:
    app_id: str
    capability_id: str
    description: str


class CapabilityKernel:
    DEVICE_ID = "cores3-se"

    def __init__(self, store: Store, now_ms: int = 0) -> None:
        self.store = store
        if self.store.connection.execute(
            "SELECT 1 FROM watch_replicas WHERE device_id=?", (self.DEVICE_ID,)
        ).fetchone() is None:
            self.replace_snapshot(self.default_snapshot(), now_ms)

    @staticmethod
    def default_snapshot() -> dict[str, Any]:
        return {
            "schema_version": 1,
            "device_id": CapabilityKernel.DEVICE_ID,
            "revision": 1,
            "foreground_app": "dev.doodad.workout",
            "route": "active_session",
            "selected_entity": "squat_set_3",
            "active_workout_id": "workout_819",
            "screen_semantics": [
                {"id": "squat_set_3", "role": "row", "label": "Squat set 3", "value": "225 lb x 5", "state": "selected"}
            ],
            "pending_jobs": [],
            "domain_state": {
                "workout": {
                    "sets": [
                        {"id": "squat_set_3", "exercise": "Squat", "weight_lb": 225, "reps": 5, "status": "pending"},
                        {"id": "squat_set_4", "exercise": "Squat", "weight_lb": 225, "reps": 5, "status": "pending"}
                    ]
                }
            },
        }

    def snapshot(self) -> dict[str, Any]:
        row = self.store.connection.execute(
            "SELECT snapshot_json FROM watch_replicas WHERE device_id=?", (self.DEVICE_ID,)
        ).fetchone()
        return json.loads(row["snapshot_json"])

    def replace_snapshot(self, snapshot: dict[str, Any], now_ms: int) -> None:
        with self.store.transaction() as connection:
            connection.execute(
                "INSERT INTO watch_replicas VALUES(?,?,?,?) ON CONFLICT(device_id) DO UPDATE SET revision=excluded.revision,snapshot_json=excluded.snapshot_json,synchronized_at_ms=excluded.synchronized_at_ms",
                (self.DEVICE_ID, int(snapshot["revision"]), Store.encode(snapshot), now_ms),
            )

    def retrieve(self, utterance: str, limit: int = 8) -> list[Capability]:
        text = utterance.lower()
        foreground = self.snapshot().get("foreground_app")
        catalog = [
            Capability("dev.doodad.workout", "record_missed_set", "Mark the selected workout set missed."),
            Capability("dev.doodad.workout", "get_next_set", "Read the next pending workout set."),
            Capability("dev.doodad.calories", "log_food", "Log a provisional food fact."),
            Capability("dev.doodad.system", "start_app_build", "Start a durable application build."),
        ]
        aliases = {
            "record_missed_set": ("missed", "skip", "set"),
            "get_next_set": ("next", "set", "workout"),
            "log_food": ("ate", "food", "bagel", "calorie"),
            "start_app_build": ("build", "make", "app", "timer"),
        }
        scored = sorted(
            catalog,
            key=lambda item: (
                -(2 if item.app_id == foreground else 0)
                - sum(1 for alias in aliases[item.capability_id] if alias in text),
                item.capability_id,
            ),
        )
        return scored[: max(1, min(limit, 8))]

    def record_missed_set(
        self,
        *,
        workout_id: str,
        set_id: str,
        expected_revision: int,
        idempotency_key: str,
        now_ms: int,
    ) -> dict[str, Any]:
        prior = self._prior(idempotency_key, "record_missed_set")
        if prior is not None:
            return {**prior, "duplicate": True}
        with self.store.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM watch_replicas WHERE device_id=?", (self.DEVICE_ID,)
            ).fetchone()
            state = json.loads(row["snapshot_json"])
            if int(row["revision"]) != expected_revision:
                raise StaleWatchState(
                    f"watch revision is {row['revision']}, expected {expected_revision}"
                )
            if state.get("foreground_app") != "dev.doodad.workout" or state.get("route") != "active_session":
                raise AmbiguousReference("Workout is not the active session")
            if state.get("active_workout_id") != workout_id or state.get("selected_entity") != set_id:
                raise StaleWatchState("selected workout set changed")
            selected = next((item for item in state["domain_state"]["workout"]["sets"] if item["id"] == set_id), None)
            if selected is None or selected["status"] != "pending":
                raise StaleWatchState("selected set is no longer pending")
            selected["status"] = "missed"
            state["revision"] = expected_revision + 1
            result = {
                "committed": True,
                "duplicate": False,
                "revision": state["revision"],
                "set_id": set_id,
                "status": "missed",
            }
            input_value = {
                "workout_id": workout_id, "set_id": set_id,
                "expected_revision": expected_revision,
            }
            connection.execute(
                "UPDATE watch_replicas SET revision=?,snapshot_json=?,synchronized_at_ms=? WHERE device_id=?",
                (state["revision"], Store.encode(state), now_ms, self.DEVICE_ID),
            )
            connection.execute(
                "INSERT INTO capability_invocations VALUES(?,?,?,?,?)",
                (idempotency_key, "record_missed_set", Store.encode(input_value), Store.encode(result), now_ms),
            )
        return result

    def get_next_set(self) -> dict[str, Any] | None:
        state = self.snapshot()
        sets = state["domain_state"]["workout"]["sets"]
        selected_id = state.get("selected_entity")
        start = next((index + 1 for index, item in enumerate(sets) if item["id"] == selected_id), 0)
        return next((dict(item) for item in sets[start:] if item["status"] == "pending"), None)

    def log_food(
        self,
        *,
        description: str,
        quantity: float,
        unit: str,
        idempotency_key: str,
        now_ms: int,
    ) -> dict[str, Any]:
        prior = self._prior(idempotency_key, "log_food")
        if prior is not None:
            return {**prior, "duplicate": True}
        entry_id = f"food_{uuid.uuid4().hex}"
        with self.store.transaction() as connection:
            row = connection.execute(
                "SELECT * FROM watch_replicas WHERE device_id=?", (self.DEVICE_ID,)
            ).fetchone()
            state = json.loads(row["snapshot_json"])
            state["revision"] = int(row["revision"]) + 1
            result = {
                "committed": True, "duplicate": False, "entry_id": entry_id,
                "description": description, "quantity": quantity, "unit": unit,
                "provisional": True, "revision": state["revision"],
            }
            connection.execute(
                "INSERT INTO nutrition_entries VALUES(?,?,?,?,?,?)",
                (entry_id, description, quantity, unit, 1, now_ms),
            )
            connection.execute(
                "UPDATE watch_replicas SET revision=?,snapshot_json=?,synchronized_at_ms=? WHERE device_id=?",
                (state["revision"], Store.encode(state), now_ms, self.DEVICE_ID),
            )
            connection.execute(
                "INSERT INTO capability_invocations VALUES(?,?,?,?,?)",
                (idempotency_key, "log_food", Store.encode({"description": description, "quantity": quantity, "unit": unit}), Store.encode(result), now_ms),
            )
        return result

    def _prior(self, key: str, capability: str) -> dict[str, Any] | None:
        row = self.store.connection.execute(
            "SELECT capability_id,result_json FROM capability_invocations WHERE idempotency_key=?", (key,)
        ).fetchone()
        if row is None:
            return None
        if row["capability_id"] != capability:
            raise ValueError("idempotency key was used by another capability")
        return json.loads(row["result_json"])
