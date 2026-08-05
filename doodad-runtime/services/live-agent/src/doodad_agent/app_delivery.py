"""HTTP delivery and reconnect-safe ``app.ready`` signaling for personal apps."""

from __future__ import annotations

import asyncio
import json
import time
import weakref
from collections.abc import Callable, Mapping
from typing import Any, Protocol
from urllib.parse import urlsplit

from aiohttp import web

from .personal_bundle import (
    BUNDLE_MEDIA_TYPE,
    ArtifactStore,
    PackagedArtifact,
    PersonalBundleError,
)
from .storage import Store


class AppReadySession(Protocol):
    async def send(
        self, message_type: str, payload: dict[str, Any] | None = None
    ) -> None: ...


class AppArtifactServer:
    """Mount immutable bundle downloads into the signaling aiohttp app."""

    ROUTE = "/apps/{bundle_sha256}"

    def __init__(self, artifacts: ArtifactStore) -> None:
        self.artifacts = artifacts

    def add_routes(self, application: web.Application) -> None:
        application.router.add_get(self.ROUTE, self.get)

    async def get(self, request: web.Request) -> web.StreamResponse:
        digest = request.match_info.get("bundle_sha256", "")
        try:
            # Content verification reads the complete immutable object. Keep
            # that disk/hash work off the WebRTC and WebSocket event loop.
            path = await asyncio.to_thread(self.artifacts.resolve, digest)
        except PersonalBundleError:
            raise web.HTTPNotFound() from None
        if path is None:
            raise web.HTTPNotFound()
        response = web.FileResponse(path)
        response.content_type = BUNDLE_MEDIA_TYPE
        response.headers["Cache-Control"] = "public, max-age=31536000, immutable"
        response.headers["X-Content-SHA256"] = digest
        response.headers["X-Content-Type-Options"] = "nosniff"
        return response


class AppReadyPublisher:
    """Announce durable ready bundles with bounded same-session retries.

    A replacement WatchSession naturally receives the announcement again, which
    lets a watch reconcile after either side reconnects. Repeated polling on one
    session is suppressed for a short interval, then retried so a transient full
    device queue or failed HTTP fetch does not permanently lose the offer.
    """

    def __init__(
        self,
        base_url: str,
        artifacts: ArtifactStore | None = None,
        *,
        owner_id: str,
        signer_key_id: str,
        retry_seconds: float = 30.0,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        normalized = base_url.rstrip("/")
        parsed = urlsplit(normalized)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise ValueError("app artifact base URL must be an absolute HTTP URL")
        if parsed.query or parsed.fragment:
            raise ValueError("app artifact base URL cannot contain query or fragment")
        if retry_seconds <= 0:
            raise ValueError("app.ready retry interval must be positive")
        if not owner_id or not signer_key_id:
            raise ValueError("app.ready publisher requires an owner and signer key ID")
        self.base_url = normalized
        self.artifacts = artifacts
        self.owner_id = owner_id
        self.signer_key_id = signer_key_id
        self.retry_seconds = retry_seconds
        self.clock = clock
        self._sent: weakref.WeakKeyDictionary[
            AppReadySession, dict[str, float]
        ] = weakref.WeakKeyDictionary()

    async def publish_pending(
        self, session: AppReadySession, store: Store, device_id: str
    ) -> list[str]:
        announced: list[str] = []
        sent = self._sent.setdefault(session, {})
        now = self.clock()
        for artifact in ready_personal_artifacts(
            store,
            device_id,
            owner_id=self.owner_id,
            signer_key_id=self.signer_key_id,
        ):
            last_sent = sent.get(artifact.bundle_sha256)
            if last_sent is not None and now - last_sent < self.retry_seconds:
                continue
            if self.artifacts is not None:
                try:
                    available = await asyncio.to_thread(
                        self.artifacts.resolve, artifact.bundle_sha256
                    )
                except PersonalBundleError:
                    available = None
                if available is None:
                    continue
            await session.send("app.ready", app_ready_payload(artifact, self.base_url))
            sent[artifact.bundle_sha256] = now
            announced.append(artifact.bundle_sha256)
        return announced


def app_ready_payload(artifact: PackagedArtifact, base_url: str) -> dict[str, Any]:
    """Return the bounded v1 watch announcement (never the host storage path)."""

    return {
        "bundle_version": artifact.bundle_version,
        "kind": artifact.kind,
        "owner_id": artifact.owner_id,
        "signer_key_id": artifact.signer_key_id,
        "app_id": artifact.app_id,
        "name": artifact.name,
        "semantic_version": artifact.semantic_version,
        "host_abi": artifact.host_abi,
        "payload_sha256": artifact.payload_sha256,
        "payload_bytes": artifact.payload_bytes,
        "bundle_sha256": artifact.bundle_sha256,
        "bundle_bytes": artifact.bundle_bytes,
        "generation_id": artifact.generation_id,
        "url": f"{base_url.rstrip('/')}/apps/{artifact.bundle_sha256}",
    }


def ready_personal_artifacts(
    store: Store,
    device_id: str,
    *,
    owner_id: str,
    signer_key_id: str,
) -> list[PackagedArtifact]:
    """Read immutable bundle handles persisted by completed Codex workers."""

    rows = store.fetch_all(
        "SELECT artifact_json FROM codex_sessions "
        "WHERE device_id=? AND stage='ready_for_review' AND artifact_json IS NOT NULL "
        "ORDER BY updated_at_ms,job_id",
        (device_id,),
    )
    latest_by_app: dict[str, PackagedArtifact] = {}
    for row in rows:
        try:
            document = json.loads(row["artifact_json"])
            if not isinstance(document, Mapping):
                continue
            bundle = document.get("bundle")
            if not isinstance(bundle, Mapping):
                continue
            artifact = PackagedArtifact.from_document(bundle)
        except (json.JSONDecodeError, PersonalBundleError, TypeError):
            continue
        # A database can outlive a local trust-profile change. Never let an old
        # owner's artifact occupy (and suppress) the current profile's per-app
        # announcement slot.
        if (
            artifact.owner_id != owner_id
            or artifact.signer_key_id != signer_key_id
        ):
            continue
        # Rows are oldest-to-newest, so a later durable generation for this
        # profile replaces the earlier announcement candidate for the app.
        latest_by_app[artifact.app_id] = artifact
    return [latest_by_app[app_id] for app_id in sorted(latest_by_app)]
