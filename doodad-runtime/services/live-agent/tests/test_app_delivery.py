from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from doodad_agent.app_delivery import (
    AppArtifactServer,
    AppReadyPublisher,
    app_ready_payload,
    ready_personal_artifacts,
)
from doodad_agent.jobs import JobManager
from doodad_agent.personal_bundle import (
    BUNDLE_MEDIA_TYPE,
    ArtifactStore,
    PersonalBundlePackager,
    PersonalTrustProfile,
)
from doodad_agent.storage import Store

from test_personal_bundle import KEY, verified_package


OWNER_ID = "nick.local"
SIGNER_KEY_ID = "macbook-v0"


class RecordingSession:
    def __init__(self) -> None:
        self.messages: list[tuple[str, dict[str, Any] | None]] = []

    async def send(
        self, message_type: str, payload: dict[str, Any] | None = None
    ) -> None:
        self.messages.append((message_type, payload))


def persist_ready_artifact(
    store: Store, device_id: str, artifact_document: dict[str, Any], now_ms: int
) -> None:
    jobs = JobManager(store, device_id)
    job_id = jobs.create("codex_app_build", {"brief": "timer"}, now_ms)
    with store.transaction() as connection:
        connection.execute(
            "INSERT INTO codex_sessions"
            "(job_id,device_id,workspace_path,codex_version,stage,artifact_json,updated_at_ms) "
            "VALUES(?,?,?,?,?,?,?)",
            (
                job_id,
                device_id,
                "/not/served/from/workspace",
                "test",
                "ready_for_review",
                Store.encode(artifact_document),
                now_ms,
            ),
        )


@pytest.mark.asyncio
async def test_artifact_get_serves_exact_immutable_object_and_rejects_unknown(
    tmp_path: Path,
) -> None:
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    packaged = PersonalBundlePackager(
        PersonalTrustProfile("nick.local", "macbook-v0", KEY), artifact_store
    ).package(verified_package(tmp_path))
    expected = Path(packaged.storage_path).read_bytes()
    application = web.Application()
    AppArtifactServer(artifact_store).add_routes(application)
    client = TestClient(TestServer(application))
    await client.start_server()
    try:
        response = await client.get(f"/apps/{packaged.bundle_sha256}")
        assert response.status == 200
        assert await response.read() == expected
        assert response.content_type == BUNDLE_MEDIA_TYPE
        assert response.headers["ETag"]
        assert response.headers["X-Content-SHA256"] == packaged.bundle_sha256
        assert response.headers["Cache-Control"].endswith("immutable")
        assert response.headers["Content-Length"] == str(packaged.bundle_bytes)

        head = await client.head(f"/apps/{packaged.bundle_sha256}")
        assert head.status == 200
        assert await head.read() == b""
        assert head.headers["Content-Length"] == str(packaged.bundle_bytes)

        missing = await client.get(f"/apps/{'0' * 64}")
        assert missing.status == 404
        malformed = await client.get("/apps/../../etc/passwd")
        assert malformed.status == 404
    finally:
        await client.close()


@pytest.mark.asyncio
async def test_corrupt_artifact_returns_not_found_instead_of_bytes(tmp_path: Path) -> None:
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    digest, path = artifact_store.put(b"once valid")
    path.write_bytes(b"tampered")
    application = web.Application()
    AppArtifactServer(artifact_store).add_routes(application)
    client = TestClient(TestServer(application))
    await client.start_server()
    try:
        response = await client.get(f"/apps/{digest}")
        assert response.status == 404
    finally:
        await client.close()


@pytest.mark.asyncio
async def test_publisher_reads_durable_ready_bundle_once_per_watch_session(
    tmp_path: Path,
) -> None:
    database = Store(tmp_path / "agent.sqlite3")
    try:
        artifact = PersonalBundlePackager(
            PersonalTrustProfile("nick.local", "macbook-v0", KEY),
            ArtifactStore(tmp_path / "artifacts"),
        ).package(verified_package(tmp_path))
        persist_ready_artifact(
            database,
            "cores3-test",
            {"artifact_id": "rest@0.1.0", "bundle": artifact.document()},
            1,
        )
        # An older verifier-only record and a malformed record are ignored.
        persist_ready_artifact(database, "cores3-test", {"sha256": "a" * 64}, 2)
        persist_ready_artifact(database, "other-watch", {"bundle": {}}, 3)

        publisher = AppReadyPublisher(
            "http://192.168.1.4:8765/",
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        )
        session = RecordingSession()
        assert await publisher.publish_pending(session, database, "cores3-test") == [
            artifact.bundle_sha256
        ]
        assert await publisher.publish_pending(session, database, "cores3-test") == []
        assert len(session.messages) == 1
        message_type, payload = session.messages[0]
        assert message_type == "app.ready"
        assert payload is not None
        assert payload["url"] == (
            f"http://192.168.1.4:8765/apps/{artifact.bundle_sha256}"
        )
        assert payload["generation_id"] == artifact.generation_id
        assert payload["payload_sha256"] == artifact.payload_sha256
        assert "storage_path" not in payload

        replacement = RecordingSession()
        assert await publisher.publish_pending(
            replacement, database, "cores3-test"
        ) == [artifact.bundle_sha256]
    finally:
        database.close()


@pytest.mark.asyncio
async def test_publisher_retries_on_same_session_after_bounded_interval(
    tmp_path: Path,
) -> None:
    database = Store(tmp_path / "agent.sqlite3")
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    now = [100.0]
    try:
        artifact = PersonalBundlePackager(
            PersonalTrustProfile("nick.local", "macbook-v0", KEY), artifact_store
        ).package(verified_package(tmp_path))
        persist_ready_artifact(
            database, "cores3-test", {"bundle": artifact.document()}, 1
        )
        publisher = AppReadyPublisher(
            "http://192.168.1.4:8765",
            artifact_store,
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
            retry_seconds=30,
            clock=lambda: now[0],
        )
        session = RecordingSession()
        assert await publisher.publish_pending(
            session, database, "cores3-test"
        ) == [artifact.bundle_sha256]
        now[0] += 29
        assert await publisher.publish_pending(session, database, "cores3-test") == []
        now[0] += 1
        assert await publisher.publish_pending(
            session, database, "cores3-test"
        ) == [artifact.bundle_sha256]
        assert len(session.messages) == 2
    finally:
        database.close()


@pytest.mark.asyncio
async def test_publisher_does_not_announce_a_missing_content_addressed_object(
    tmp_path: Path,
) -> None:
    database = Store(tmp_path / "agent.sqlite3")
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    try:
        artifact = PersonalBundlePackager(
            PersonalTrustProfile("nick.local", "macbook-v0", KEY), artifact_store
        ).package(verified_package(tmp_path))
        persist_ready_artifact(
            database, "cores3-test", {"bundle": artifact.document()}, 1
        )
        Path(artifact.storage_path).unlink()
        session = RecordingSession()
        publisher = AppReadyPublisher(
            "http://192.168.1.4:8765",
            artifact_store,
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        )
        assert await publisher.publish_pending(
            session, database, "cores3-test"
        ) == []
        assert session.messages == []
    finally:
        database.close()


def test_ready_artifact_query_deduplicates_bundle_and_is_device_scoped(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    try:
        artifact = PersonalBundlePackager(
            PersonalTrustProfile("nick.local", "macbook-v0", KEY),
            ArtifactStore(tmp_path / "artifacts"),
        ).package(verified_package(tmp_path))
        document = {"bundle": artifact.document()}
        persist_ready_artifact(store, "cores3-one", document, 1)
        persist_ready_artifact(store, "cores3-one", document, 2)
        persist_ready_artifact(store, "cores3-two", document, 3)
        assert ready_personal_artifacts(
            store,
            "cores3-one",
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        ) == [artifact]
        assert ready_personal_artifacts(
            store,
            "missing",
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        ) == []
    finally:
        store.close()


def test_ready_artifact_query_announces_only_latest_generation_per_app(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    packager = PersonalBundlePackager(
        PersonalTrustProfile("nick.local", "macbook-v0", KEY), artifact_store
    )
    try:
        older = packager.package(verified_package(tmp_path / "old", b"\0asm-old"))
        latest = packager.package(verified_package(tmp_path / "new", b"\0asm-new"))
        assert older.app_id == latest.app_id
        assert older.bundle_sha256 != latest.bundle_sha256
        persist_ready_artifact(store, "cores3-one", {"bundle": older.document()}, 1)
        persist_ready_artifact(store, "cores3-one", {"bundle": latest.document()}, 2)
        assert ready_personal_artifacts(
            store,
            "cores3-one",
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        ) == [latest]
    finally:
        store.close()


def test_ready_artifact_query_excludes_old_profile_before_latest_app_dedup(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    artifact_store = ArtifactStore(tmp_path / "artifacts")
    try:
        current = PersonalBundlePackager(
            PersonalTrustProfile(OWNER_ID, SIGNER_KEY_ID, KEY), artifact_store
        ).package(verified_package(tmp_path / "current", b"\0asm-current"))
        old_profile = PersonalBundlePackager(
            PersonalTrustProfile("other.local", "old-key", b"z" * 32),
            artifact_store,
        ).package(verified_package(tmp_path / "old-profile", b"\0asm-old-profile"))
        assert current.app_id == old_profile.app_id
        persist_ready_artifact(store, "cores3-one", {"bundle": current.document()}, 1)
        # This row is newer, but belongs to a different configured profile and
        # must not suppress the current owner's candidate for the same app ID.
        persist_ready_artifact(
            store, "cores3-one", {"bundle": old_profile.document()}, 2
        )

        assert ready_personal_artifacts(
            store,
            "cores3-one",
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        ) == [current]
    finally:
        store.close()


def test_app_ready_payload_is_bounded_and_does_not_disclose_host_path(
    tmp_path: Path,
) -> None:
    artifact = PersonalBundlePackager(
        PersonalTrustProfile("nick.local", "macbook-v0", KEY),
        ArtifactStore(tmp_path / "artifacts"),
    ).package(verified_package(tmp_path))
    payload = app_ready_payload(artifact, "http://doodad.local:8765")
    encoded = json.dumps(payload, separators=(",", ":"))
    assert len(encoded.encode()) < 2_048
    assert artifact.storage_path not in encoded
    assert payload["bundle_sha256"] in payload["url"]


@pytest.mark.parametrize(
    "url",
    ["doodad.local:8765", "ftp://doodad.local", "http://host/path?token=x"],
)
def test_publisher_rejects_ambiguous_base_urls(url: str) -> None:
    with pytest.raises(ValueError):
        AppReadyPublisher(
            url,
            owner_id=OWNER_ID,
            signer_key_id=SIGNER_KEY_ID,
        )
