from __future__ import annotations

from pathlib import Path

from doodad_agent.storage import LATEST_SCHEMA_VERSION, LEGACY_DEVICE_ID, Store


def test_v2_schema_and_first_cores3_connection_relink_legacy_rows(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    with store.transaction() as connection:
        connection.execute(
            "INSERT INTO capability_invocations VALUES(?,?,?,?,?,?)",
            (LEGACY_DEVICE_ID, "same-key", "demo", "{}", "{}", 1),
        )
        connection.execute(
            "INSERT INTO nutrition_entries VALUES(?,?,?,?,?,?,?)",
            ("entry", LEGACY_DEVICE_ID, "apple", 1.0, "item", 0, 1),
        )
        connection.execute(
            "INSERT INTO watch_replicas VALUES(?,?,?,?)",
            ("cores3-se", 7, '{"surface":"home"}', 1),
        )

    stable_id = "cores3-se-aabbccddeeff"
    store.relink_legacy_device(stable_id)

    assert store.connection.execute("PRAGMA user_version").fetchone()[0] == (
        LATEST_SCHEMA_VERSION
    )
    assert store.connection.execute(
        "SELECT device_id FROM capability_invocations"
    ).fetchone()[0] == stable_id
    assert store.connection.execute(
        "SELECT device_id FROM nutrition_entries"
    ).fetchone()[0] == stable_id
    replica = store.connection.execute(
        "SELECT device_id,revision FROM watch_replicas"
    ).fetchone()
    assert tuple(replica) == (stable_id, 7)
    assert store.connection.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='codex_sessions'"
    ).fetchone() is not None
    assert "approved_plan_sha256" in store._columns("codex_sessions")
    assert "design_target_sha256" in store._columns("codex_sessions")
    store.close()
