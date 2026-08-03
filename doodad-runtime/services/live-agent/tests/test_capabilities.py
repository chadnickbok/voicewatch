from __future__ import annotations

import pytest

from doodad_agent.capabilities import CapabilityKernel, StaleWatchState
from doodad_agent.storage import Store


def test_missed_set_commits_exactly_once_and_rejects_stale(tmp_path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    kernel = CapabilityKernel(store)
    state = kernel.snapshot()
    result = kernel.record_missed_set(
        workout_id=state["active_workout_id"],
        set_id=state["selected_entity"],
        expected_revision=state["revision"],
        idempotency_key="utterance-missed-set-001",
        now_ms=100,
    )
    assert result == {
        "committed": True,
        "duplicate": False,
        "revision": 2,
        "set_id": "squat_set_3",
        "status": "missed",
    }
    duplicate = kernel.record_missed_set(
        workout_id=state["active_workout_id"],
        set_id=state["selected_entity"],
        expected_revision=state["revision"],
        idempotency_key="utterance-missed-set-001",
        now_ms=101,
    )
    assert duplicate["duplicate"] is True
    assert store.connection.execute("SELECT COUNT(*) FROM capability_invocations").fetchone()[0] == 1
    assert kernel.get_next_set()["id"] == "squat_set_4"

    with pytest.raises(StaleWatchState):
        kernel.record_missed_set(
            workout_id=state["active_workout_id"],
            set_id=state["selected_entity"],
            expected_revision=1,
            idempotency_key="utterance-missed-set-002",
            now_ms=102,
        )


def test_food_log_is_provisional_and_idempotent(tmp_path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    kernel = CapabilityKernel(store)
    first = kernel.log_food(
        description="bagel", quantity=1, unit="item",
        idempotency_key="utterance-food-bagel-001", now_ms=200,
    )
    second = kernel.log_food(
        description="bagel", quantity=1, unit="item",
        idempotency_key="utterance-food-bagel-001", now_ms=201,
    )
    assert first["provisional"] is True
    assert second["duplicate"] is True
    assert store.connection.execute("SELECT COUNT(*) FROM nutrition_entries").fetchone()[0] == 1


def test_retrieval_is_bounded_and_foreground_aware(tmp_path) -> None:
    kernel = CapabilityKernel(Store(tmp_path / "agent.sqlite3"))
    tools = kernel.retrieve("I missed that set", limit=99)
    assert len(tools) <= 8
    assert tools[0].capability_id == "record_missed_set"
