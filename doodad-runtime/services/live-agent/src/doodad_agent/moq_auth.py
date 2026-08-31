"""Short-lived, one-use MoQ grants bound to an authenticated control session.

This is the issuer for VoiceWatch's own endpoint, not an assumed third-party
JWT dialect. Endpoint authorization must use the private IPC redemption method;
opaque tokens cannot be used against an arbitrary public relay. All methods
run on one asyncio loop and contain no awaits, making redemption atomic.
"""
from __future__ import annotations

import hashlib
import hmac
import json
import math
import os
import re
import secrets
import stat
import time
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path

DEVICE_ID = re.compile(r"[a-z0-9][a-z0-9._:-]{7,63}\Z")
TOKEN = re.compile(r"[A-Za-z0-9_-]{43}\Z")
PROOF_DOMAIN = b"voicewatch-moq-bootstrap-v1\0"


class AuthorizationError(Exception):
    """Fixed, credential-free failure text safe for boundary handlers."""

    def __init__(self) -> None:
        super().__init__("MoQ authorization denied")


def bootstrap_proof(key: bytes, device_id: str, challenge: str) -> str:
    """Enrollment proof format shared with firmware; no client clock required."""
    body = PROOF_DOMAIN + device_id.encode("ascii") + b"\0" + challenge.encode("ascii")
    return hmac.new(key, body, hashlib.sha256).hexdigest()


def renewal_proof(key: bytes, device_id: str, session_id: str, nonce: str) -> str:
    """A different signing domain; a bootstrap proof cannot renew a session."""
    body = f'voicewatch-moq-renew-v1\0{device_id}\0{session_id}\0{nonce}'.encode('ascii')
    return hmac.new(key, body, hashlib.sha256).hexdigest()


def load_device_keys(path: Path) -> dict[str, bytes]:
    """Read a bounded, owner-only enrollment file without following symlinks."""
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        meta = os.fstat(fd)
        if (not stat.S_ISREG(meta.st_mode) or meta.st_uid != os.getuid()
                or meta.st_mode & 0o077 or meta.st_size > 64 * 1024):
            raise ValueError("enrollment file must be owner-only, regular and at most 64 KiB")
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError("duplicate enrollment entry")
                result[key] = value
            return result
        with os.fdopen(fd, "rb", closefd=False) as source:
            data = json.loads(source.read(64 * 1024 + 1), object_pairs_hook=unique)
        if not isinstance(data, dict) or not 1 <= len(data) <= 256:
            raise ValueError("enrollment file must contain 1..256 devices")
        keys = {}
        for device, value in data.items():
            if not DEVICE_ID.fullmatch(device) or not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
                raise ValueError("invalid enrollment entry")
            keys[device] = bytes.fromhex(value)
        if len(set(keys.values())) != len(keys):
            raise ValueError("each device requires a unique enrollment key")
        return keys
    except (json.JSONDecodeError, UnicodeError):
        raise ValueError("invalid enrollment file") from None
    finally:
        os.close(fd)


@dataclass(frozen=True, repr=False)
class IssuedGrant:
    session_id: str
    device_id: str
    publish: str
    subscribe: str
    expires_unix: int
    lease_seconds: int
    control_token: str = field(repr=False)
    media_token: str = field(repr=False)

    def document(self) -> dict[str, object]:
        return {
            "session_id": self.session_id, "device_id": self.device_id,
            "publish": self.publish, "subscribe": self.subscribe,
            "expires_unix": self.expires_unix, "lease_seconds": self.lease_seconds,
            "control_token": self.control_token,
            "setup_path": "/voicewatch/v1?token=" + self.media_token,
        }


@dataclass(repr=False)
class _Grant:
    session_id: str
    device_id: str
    publish: str
    subscribe: str
    until: float
    attach_until: float
    expires_unix: int
    control_hash: bytes
    media_hash: bytes
    control_owner: object | None = None
    media_owner: object | None = None
    renewal: tuple[str, float] | None = None
    renewal_revision: int = 0


def _digest(token: str) -> bytes:
    if not isinstance(token, str) or not TOKEN.fullmatch(token):
        raise AuthorizationError()
    return hashlib.sha256(token.encode("ascii")).digest()


class GrantRegistry:
    """In-memory grants vanish on restart; reconnect always needs a fresh proof."""

    def __init__(self, device_keys: Mapping[str, bytes], *, lease_seconds: int = 300,
                 attach_seconds: int = 30, capacity: int = 256,
                 monotonic: Callable[[], float] = time.monotonic,
                 wall_clock: Callable[[], float] = time.time) -> None:
        if not 1 <= attach_seconds <= lease_seconds <= 900 or not 1 <= capacity <= 1024:
            raise ValueError("invalid grant limits")
        if (not 1 <= len(device_keys) <= 256 or any(
                not DEVICE_ID.fullmatch(device) or not isinstance(key, bytes) or len(key) != 32
                for device, key in device_keys.items())
                or len(set(device_keys.values())) != len(device_keys)):
            raise ValueError("unique 256-bit per-device enrollment keys required")
        self._keys = dict(device_keys)
        self._clock, self._wall = monotonic, wall_clock
        self._lease, self._attach, self._capacity = lease_seconds, attach_seconds, capacity
        self._last_wall = wall_clock()
        self._last_mono = monotonic()
        self._challenges: dict[str, tuple[str, float]] = {}
        self._grants: dict[str, _Grant] = {}

    def _now(self) -> float:
        now, wall = self._clock(), self._wall()
        if (not math.isfinite(now) or not math.isfinite(wall)
                or now < self._last_mono or wall < self._last_wall - 1):
            self._grants.clear()
            self._challenges.clear()
            # Fail closed until the operator corrects the host clock.
            raise AuthorizationError()
        self._last_wall = max(self._last_wall, wall)
        self._last_mono = now
        self._challenges = {key: item for key, item in self._challenges.items() if item[1] > now}
        self._grants = {key: item for key, item in self._grants.items()
                        if item.until > now and item.expires_unix > wall
                        and (item.media_owner is not None or item.attach_until > now)}
        return now

    def challenge(self, device_id: str) -> str:
        now = self._now()
        if (not isinstance(device_id, str) or device_id not in self._keys
                or len(self._challenges) >= self._capacity):
            raise AuthorizationError()
        # Bound both total work and retained challenge state. Replacing an
        # unredeemed challenge deliberately invalidates it, without touching an
        # active control/media session.
        challenge = secrets.token_urlsafe(32)
        self._challenges[device_id] = (challenge, now + self._attach)
        return challenge

    def time_proof(self, device_id: str, nonce: str) -> dict[str, object]:
        """Authenticate cold-start time, without issuing any session capability.

        The device generates a fresh 256-bit nonce, measures the round trip and
        verifies this domain-separated MAC before changing its clock. A caller
        cannot choose a timestamp, lifetime or signing domain.
        """
        self._now()
        if (not isinstance(device_id, str) or device_id not in self._keys
                or not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{64}', nonce)):
            raise AuthorizationError()
        unix_ms, validity = str(int(self._wall() * 1000)), 600
        if not 1_700_000_000_000 <= int(unix_ms) <= 4_102_444_800_000:
            raise AuthorizationError()
        body = f'voicewatch-moq-time-v1\0{device_id}\0{nonce}\0{unix_ms}\0{validity}'.encode('ascii')
        proof = hmac.new(self._keys[device_id], body, hashlib.sha256).hexdigest()
        return dict(v=1, device_id=device_id, nonce=nonce, unix_ms=unix_ms,
                    validity_seconds=validity, proof=proof)

    def issue(self, device_id: str, challenge: str, proof: str) -> IssuedGrant:
        now = self._now()
        if not isinstance(device_id, str):
            raise AuthorizationError()
        pending = self._challenges.get(device_id)
        if (not pending or not isinstance(challenge, str) or not TOKEN.fullmatch(challenge)
                or not hmac.compare_digest(pending[0], challenge)):
            raise AuthorizationError()
        # Even a bad proof consumes the matched nonce, limiting online guesses.
        del self._challenges[device_id]
        if (not isinstance(proof, str) or not re.fullmatch(r"[0-9a-f]{64}", proof)
                or not hmac.compare_digest(bootstrap_proof(self._keys[device_id], device_id, challenge), proof)
                or len(self._grants) >= self._capacity):
            raise AuthorizationError()
        sid = secrets.token_hex(16)
        control, media = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        prefix = f"voicewatch/{device_id}/{sid}"
        expires = int(self._wall()) + self._lease
        grant = _Grant(sid, device_id, prefix + "/watch", prefix + "/agent",
                       now + self._lease, now + self._attach, expires,
                       _digest(control), _digest(media))
        self._grants[sid] = grant
        return IssuedGrant(sid, device_id, grant.publish, grant.subscribe,
                           expires, self._lease, control, media)

    def activate_control(self, token: str, owner: object) -> str:
        self._now()
        if owner is None:
            raise AuthorizationError()
        digest = _digest(token)
        for grant in self._grants.values():
            if hmac.compare_digest(grant.control_hash, digest) and grant.control_owner is None:
                grant.control_owner = owner
                # Replacing one device revokes its previous control AND media
                # grants. Holders must check valid() or the liveness sweeper.
                for sid, prior in list(self._grants.items()):
                    if prior.device_id == grant.device_id and sid != grant.session_id:
                        del self._grants[sid]
                return grant.session_id
        raise AuthorizationError()

    def attach_media(self, token: str, owner: object) -> dict[str, object]:
        now = self._now()
        if owner is None:
            raise AuthorizationError()
        digest = _digest(token)
        for grant in self._grants.values():
            if (hmac.compare_digest(grant.media_hash, digest) and grant.control_owner is not None
                    and grant.media_owner is None and grant.attach_until > now):
                grant.media_owner = owner
                return {"session_id": grant.session_id, "device_id": grant.device_id,
                        "publish": grant.publish, "subscribe": grant.subscribe,
                        "lease_ms": max(0, int(min(grant.until - now,
                                                  grant.expires_unix - self._wall()) * 1000))}
        raise AuthorizationError()

    def identity(self, session_id: str, control_owner: object) -> str:
        self._now()
        grant = self._grants.get(session_id)
        if control_owner is None or grant is None or grant.control_owner is not control_owner:
            raise AuthorizationError()
        return grant.device_id

    def renewal_challenge(self, session_id: str, control_owner: object) -> str | None:
        """One bounded challenge at half lease, only for the attached owner.

        Sending/receiving a challenge does not extend anything. A timed-out
        exchange fails closed instead of issuing unlimited retry challenges.
        """
        now = self._now()
        grant = self._grants.get(session_id)
        if control_owner is None or grant is None or grant.control_owner is not control_owner:
            raise AuthorizationError()
        if grant.media_owner is None:
            raise AuthorizationError()
        if grant.renewal is not None:
            if now >= grant.renewal[1]:
                raise AuthorizationError()
            return None
        if min(grant.until-now, grant.expires_unix-self._wall()) > self._lease / 2:
            return None
        nonce = secrets.token_hex(32)
        grant.renewal = nonce, min(grant.until, now+10)
        return nonce

    def renew(self, session_id: str, control_owner: object, nonce: str, proof: str) -> dict[str, object]:
        now = self._now()
        grant = self._grants.get(session_id)
        if control_owner is None or grant is None or grant.control_owner is not control_owner:
            raise AuthorizationError()
        pending = grant.renewal
        if (grant.media_owner is None or pending is None or pending[1] <= now
                or not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{64}', nonce)
                or not hmac.compare_digest(pending[0], nonce)):
            raise AuthorizationError()
        grant.renewal = None  # Even a bad matching proof consumes its nonce.
        if (not isinstance(proof, str) or not re.fullmatch('[0-9a-f]{64}', proof)
                or not hmac.compare_digest(proof, renewal_proof(
                    self._keys[grant.device_id], grant.device_id, session_id, nonce))
                or grant.renewal_revision >= 2**53-1):
            raise AuthorizationError()
        time_proof = self.time_proof(grant.device_id, nonce)
        expires = int(self._wall()) + self._lease
        grant.until, grant.expires_unix = now+self._lease, expires
        grant.renewal_revision += 1
        return dict(nonce=nonce, revision=grant.renewal_revision,
                    expires_unix=expires, lease_seconds=self._lease, time=time_proof)

    def valid(self, session_id: str, owner: object) -> bool:
        try:
            self._now()
        except AuthorizationError:
            return False
        grant = self._grants.get(session_id)
        return owner is not None and grant is not None and (grant.control_owner is owner or grant.media_owner is owner)

    def revoke(self, session_id: str, owner: object) -> None:
        grant = self._grants.get(session_id)
        if owner is not None and grant is not None and (grant.control_owner is owner or grant.media_owner is owner):
            del self._grants[session_id]
