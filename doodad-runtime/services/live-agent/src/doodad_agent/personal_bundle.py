"""Personal-app bundle v1 and durable content-addressed artifact storage.

The personal profile is intentionally a small shared-secret trust boundary for the
single-user development loop.  Codex never receives this key: packaging happens
only after the independent verifier has returned a :class:`VerifiedArtifact`.
"""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import re
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from .app_verifier import VerifiedArtifact, package_tree_snapshot


BUNDLE_MAGIC = b"DDB1"
BUNDLE_HEADER = struct.Struct(">4sII")
BUNDLE_TAG_BYTES = hashlib.sha256().digest_size
BUNDLE_HMAC_DOMAIN = b"Doodad Personal Bundle v1\0"
BUNDLE_MEDIA_TYPE = "application/vnd.doodad.personal-bundle-v1"
MAX_METADATA_BYTES = 16 * 1024
MAX_PAYLOAD_BYTES = 1 * 1024 * 1024

_DIGEST_PATTERN = re.compile(r"^[0-9a-f]{64}$")
_KEY_HEX_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
_IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_APP_ID_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:\.[a-z0-9][a-z0-9-]*)+$")
_VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$")

_METADATA_KEYS = frozenset(
    {
        "bundle_version",
        "kind",
        "owner_id",
        "signer_key_id",
        "app_id",
        "name",
        "semantic_version",
        "host_abi",
        "payload_sha256",
        "payload_bytes",
    }
)


class PersonalBundleError(RuntimeError):
    """The verified output could not be represented as a personal bundle."""


@dataclass(frozen=True)
class PersonalTrustProfile:
    """Explicit owner and shared key used for personal development bundles."""

    owner_id: str
    signer_key_id: str
    hmac_key: bytes

    def __post_init__(self) -> None:
        _validate_identifier("owner_id", self.owner_id)
        _validate_identifier("signer_key_id", self.signer_key_id)
        _validate_hmac_key(self.hmac_key)

    @classmethod
    def from_hex(cls, owner_id: str, signer_key_id: str, key_hex: str) -> "PersonalTrustProfile":
        if _KEY_HEX_PATTERN.fullmatch(key_hex) is None:
            raise PersonalBundleError(
                "personal HMAC key must be exactly 64 hexadecimal characters"
            )
        try:
            key = bytes.fromhex(key_hex)
        except ValueError as error:
            raise PersonalBundleError("personal HMAC key must be hexadecimal") from error
        return cls(owner_id, signer_key_id, key)


@dataclass(frozen=True)
class PackagedArtifact:
    """Serializable handle for one immutable personal bundle."""

    bundle_version: int
    kind: str
    owner_id: str
    signer_key_id: str
    app_id: str
    name: str
    semantic_version: str
    host_abi: int
    payload_sha256: str
    payload_bytes: int
    bundle_sha256: str
    bundle_bytes: int
    storage_path: str

    @property
    def generation_id(self) -> str:
        return f"{self.app_id}@{self.semantic_version}+{self.payload_sha256}"

    def document(self) -> dict[str, Any]:
        return {
            "bundle_version": self.bundle_version,
            "kind": self.kind,
            "owner_id": self.owner_id,
            "signer_key_id": self.signer_key_id,
            "app_id": self.app_id,
            "name": self.name,
            "semantic_version": self.semantic_version,
            "host_abi": self.host_abi,
            "payload_sha256": self.payload_sha256,
            "payload_bytes": self.payload_bytes,
            "bundle_sha256": self.bundle_sha256,
            "bundle_bytes": self.bundle_bytes,
            "storage_path": self.storage_path,
            "generation_id": self.generation_id,
        }

    @classmethod
    def from_document(cls, value: Mapping[str, Any]) -> "PackagedArtifact":
        try:
            strings = {
                key: value[key]
                for key in (
                    "kind",
                    "owner_id",
                    "signer_key_id",
                    "app_id",
                    "name",
                    "semantic_version",
                    "payload_sha256",
                    "bundle_sha256",
                    "storage_path",
                )
            }
            integers = {
                key: value[key]
                for key in (
                    "bundle_version",
                    "host_abi",
                    "payload_bytes",
                    "bundle_bytes",
                )
            }
            if not all(isinstance(item, str) for item in strings.values()) or not all(
                isinstance(item, int) and not isinstance(item, bool)
                for item in integers.values()
            ):
                raise TypeError("packaged-artifact fields have invalid types")
            artifact = cls(
                bundle_version=integers["bundle_version"],
                kind=strings["kind"],
                owner_id=strings["owner_id"],
                signer_key_id=strings["signer_key_id"],
                app_id=strings["app_id"],
                name=strings["name"],
                semantic_version=strings["semantic_version"],
                host_abi=integers["host_abi"],
                payload_sha256=strings["payload_sha256"],
                payload_bytes=integers["payload_bytes"],
                bundle_sha256=strings["bundle_sha256"],
                bundle_bytes=integers["bundle_bytes"],
                storage_path=strings["storage_path"],
            )
        except (KeyError, TypeError, ValueError) as error:
            raise PersonalBundleError("invalid packaged-artifact document") from error
        artifact._validate()
        return artifact

    def _validate(self) -> None:
        if self.bundle_version != 1 or self.kind != "personal":
            raise PersonalBundleError("unsupported personal bundle identity")
        _validate_identifier("owner_id", self.owner_id)
        _validate_identifier("signer_key_id", self.signer_key_id)
        _validate_app_id(self.app_id)
        _validate_version(self.semantic_version)
        if not self.name or len(self.name) > 48:
            raise PersonalBundleError("app name must contain 1..48 characters")
        if self.host_abi < 1 or self.host_abi > 0xFFFFFFFF:
            raise PersonalBundleError("host_abi is outside the uint32 range")
        _validate_digest("payload_sha256", self.payload_sha256)
        _validate_digest("bundle_sha256", self.bundle_sha256)
        if not 1 <= self.payload_bytes <= MAX_PAYLOAD_BYTES:
            raise PersonalBundleError("payload size is outside the v1 limit")
        metadata = {
            "bundle_version": self.bundle_version,
            "kind": self.kind,
            "owner_id": self.owner_id,
            "signer_key_id": self.signer_key_id,
            "app_id": self.app_id,
            "name": self.name,
            "semantic_version": self.semantic_version,
            "host_abi": self.host_abi,
            "payload_sha256": self.payload_sha256,
            "payload_bytes": self.payload_bytes,
        }
        expected_bundle_bytes = (
            BUNDLE_HEADER.size
            + len(_canonical_json(metadata))
            + self.payload_bytes
            + BUNDLE_TAG_BYTES
        )
        if self.bundle_bytes != expected_bundle_bytes:
            raise PersonalBundleError("bundle size is inconsistent with its envelope")
        if not self.storage_path:
            raise PersonalBundleError("artifact storage path is empty")


class ArtifactStore:
    """Write-once SHA-256 object store outside mutable Codex workspaces."""

    def __init__(self, root: Path) -> None:
        self.root = root.expanduser().resolve()
        self.objects = self.root / "objects"
        self.incoming = self.root / ".incoming"
        self.objects.mkdir(parents=True, exist_ok=True)
        self.incoming.mkdir(parents=True, exist_ok=True)

    def put(self, bundle: bytes) -> tuple[str, Path]:
        if not bundle:
            raise PersonalBundleError("cannot store an empty bundle")
        digest = hashlib.sha256(bundle).hexdigest()
        destination = self.path_for(digest)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if (
                destination.is_symlink()
                or _file_sha256(destination) != digest
                or destination.read_bytes() != bundle
            ):
                raise PersonalBundleError(
                    f"content-addressed object is corrupt: {digest}"
                )
            return digest, destination

        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f"{digest}.", suffix=".part", dir=self.incoming
        )
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(bundle)
                stream.flush()
                os.fsync(stream.fileno())
            os.chmod(temporary, 0o644)
            # Identical concurrent writers are harmless; replacing a destination
            # with the same digest preserves the write-once content invariant.
            os.replace(temporary, destination)
            _fsync_directory(destination.parent)
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
        return digest, destination

    def path_for(self, bundle_sha256: str) -> Path:
        _validate_digest("bundle_sha256", bundle_sha256)
        return self.objects / bundle_sha256[:2] / f"{bundle_sha256}.ddb"

    def resolve(self, bundle_sha256: str) -> Path | None:
        path = self.path_for(bundle_sha256)
        if path.is_symlink() or not path.is_file():
            return None
        if _file_sha256(path) != bundle_sha256:
            raise PersonalBundleError(
                f"content-addressed object failed its digest check: {bundle_sha256}"
            )
        return path


class PersonalBundlePackager:
    """Promote verified Wasm to an owner-bound immutable personal artifact."""

    def __init__(self, profile: PersonalTrustProfile, store: ArtifactStore) -> None:
        self.profile = profile
        self.store = store

    def package(self, verified: VerifiedArtifact) -> PackagedArtifact:
        package = Path(verified.package_path).resolve()
        if package.is_relative_to(self.store.root) or self.store.root.is_relative_to(
            package
        ):
            raise PersonalBundleError(
                "artifact storage must be separate from the mutable verified package"
            )
        try:
            snapshot_sha256, snapshot = package_tree_snapshot(package)
        except OSError as error:
            raise PersonalBundleError(
                f"cannot snapshot verified package: {error}"
            ) from error
        if not hmac.compare_digest(snapshot_sha256, verified.sha256):
            raise PersonalBundleError(
                "verified package changed before outer packaging"
            )
        manifest_bytes = snapshot.get("manifest.json")
        payload = snapshot.get("app.wasm")
        if manifest_bytes is None or payload is None:
            raise PersonalBundleError(
                "verified package must contain manifest.json and app.wasm"
            )
        manifest = _decode_json_object(manifest_bytes, "manifest.json")
        if manifest.get("wasm") != "app.wasm":
            raise PersonalBundleError("verified manifest must name app.wasm")
        if not 1 <= len(payload) <= MAX_PAYLOAD_BYTES:
            raise PersonalBundleError(
                f"app.wasm size must be within 1..{MAX_PAYLOAD_BYTES} bytes"
            )

        metadata = _metadata_from_manifest(self.profile, manifest, payload)
        expected_artifact_id = (
            f"{metadata['app_id']}@{metadata['semantic_version']}"
        )
        if verified.artifact_id != expected_artifact_id:
            raise PersonalBundleError(
                "verified artifact identity does not match its staged manifest"
            )
        bundle = encode_personal_bundle(metadata, payload, self.profile.hmac_key)
        bundle_digest, bundle_path = self.store.put(bundle)
        artifact = PackagedArtifact(
            bundle_version=1,
            kind="personal",
            owner_id=self.profile.owner_id,
            signer_key_id=self.profile.signer_key_id,
            app_id=str(metadata["app_id"]),
            name=str(metadata["name"]),
            semantic_version=str(metadata["semantic_version"]),
            host_abi=int(metadata["host_abi"]),
            payload_sha256=str(metadata["payload_sha256"]),
            payload_bytes=len(payload),
            bundle_sha256=bundle_digest,
            bundle_bytes=len(bundle),
            storage_path=str(bundle_path),
        )
        artifact._validate()
        return artifact


def encode_personal_bundle(
    metadata: Mapping[str, Any], payload: bytes, hmac_key: bytes
) -> bytes:
    """Encode the exact cross-platform DDB1 wire representation."""

    _validate_hmac_key(hmac_key)
    normalized = dict(metadata)
    _validate_metadata(normalized, payload)
    metadata_bytes = _canonical_json(normalized)
    if len(metadata_bytes) > MAX_METADATA_BYTES:
        raise PersonalBundleError("personal bundle metadata exceeds the v1 limit")
    header = BUNDLE_HEADER.pack(BUNDLE_MAGIC, len(metadata_bytes), len(payload))
    signed = header + metadata_bytes + payload
    tag = hmac.digest(hmac_key, BUNDLE_HMAC_DOMAIN + signed, "sha256")
    return signed + tag


def decode_personal_bundle(bundle: bytes, hmac_key: bytes) -> tuple[dict[str, Any], bytes]:
    """Strictly parse and authenticate a DDB1 bundle (also used by tests/tools)."""

    _validate_hmac_key(hmac_key)
    minimum = BUNDLE_HEADER.size + BUNDLE_TAG_BYTES + 1
    if len(bundle) < minimum:
        raise PersonalBundleError("personal bundle is truncated")
    magic, metadata_length, payload_length = BUNDLE_HEADER.unpack_from(bundle)
    if magic != BUNDLE_MAGIC:
        raise PersonalBundleError("personal bundle magic/version is unsupported")
    if metadata_length > MAX_METADATA_BYTES or payload_length > MAX_PAYLOAD_BYTES:
        raise PersonalBundleError("personal bundle declares an oversized section")
    expected_length = (
        BUNDLE_HEADER.size + metadata_length + payload_length + BUNDLE_TAG_BYTES
    )
    if len(bundle) != expected_length:
        raise PersonalBundleError("personal bundle length does not match its header")
    metadata_start = BUNDLE_HEADER.size
    payload_start = metadata_start + metadata_length
    tag_start = payload_start + payload_length
    metadata_bytes = bundle[metadata_start:payload_start]
    payload = bundle[payload_start:tag_start]
    expected_tag = hmac.digest(
        hmac_key, BUNDLE_HMAC_DOMAIN + bundle[:tag_start], "sha256"
    )
    if not hmac.compare_digest(bundle[tag_start:], expected_tag):
        raise PersonalBundleError("personal bundle signature is invalid")
    try:
        value = json.loads(metadata_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PersonalBundleError("personal bundle metadata is not UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise PersonalBundleError("personal bundle metadata must be an object")
    if _canonical_json(value) != metadata_bytes:
        raise PersonalBundleError("personal bundle metadata is not canonical JSON")
    _validate_metadata(value, payload)
    return value, payload


def _metadata_from_manifest(
    profile: PersonalTrustProfile, manifest: Mapping[str, Any], payload: bytes
) -> dict[str, Any]:
    app_id = manifest.get("id")
    name = manifest.get("name")
    version = manifest.get("version")
    host_abi = manifest.get("host_abi")
    if (
        not isinstance(app_id, str)
        or not isinstance(name, str)
        or not isinstance(version, str)
        or isinstance(host_abi, bool)
        or not isinstance(host_abi, int)
    ):
        raise PersonalBundleError("verified manifest lacks a typed package identity")
    return {
        "bundle_version": 1,
        "kind": "personal",
        "owner_id": profile.owner_id,
        "signer_key_id": profile.signer_key_id,
        "app_id": app_id,
        "name": name,
        "semantic_version": version,
        "host_abi": host_abi,
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "payload_bytes": len(payload),
    }


def _validate_metadata(metadata: Mapping[str, Any], payload: bytes) -> None:
    if not 1 <= len(payload) <= MAX_PAYLOAD_BYTES:
        raise PersonalBundleError(
            f"payload length must be within 1..{MAX_PAYLOAD_BYTES} bytes"
        )
    if frozenset(metadata) != _METADATA_KEYS:
        missing = sorted(_METADATA_KEYS - frozenset(metadata))
        extra = sorted(frozenset(metadata) - _METADATA_KEYS)
        raise PersonalBundleError(
            f"personal bundle metadata keys do not match v1 (missing={missing}, extra={extra})"
        )
    bundle_version = metadata.get("bundle_version")
    if (
        isinstance(bundle_version, bool)
        or not isinstance(bundle_version, int)
        or bundle_version != 1
        or metadata.get("kind") != "personal"
    ):
        raise PersonalBundleError("personal bundle metadata has unsupported identity")
    for key in ("owner_id", "signer_key_id"):
        value = metadata.get(key)
        if not isinstance(value, str):
            raise PersonalBundleError(f"{key} must be a string")
        _validate_identifier(key, value)
    app_id = metadata.get("app_id")
    if not isinstance(app_id, str):
        raise PersonalBundleError("app_id must be a string")
    _validate_app_id(app_id)
    name = metadata.get("name")
    if (
        not isinstance(name, str)
        or not name
        or len(name) > 48
        or any(ord(character) < 0x20 or ord(character) == 0x7F for character in name)
    ):
        raise PersonalBundleError(
            "name must contain 1..48 printable Unicode characters"
        )
    version = metadata.get("semantic_version")
    if not isinstance(version, str):
        raise PersonalBundleError("semantic_version must be a string")
    _validate_version(version)
    host_abi = metadata.get("host_abi")
    if isinstance(host_abi, bool) or not isinstance(host_abi, int) or not 1 <= host_abi <= 0xFFFFFFFF:
        raise PersonalBundleError("host_abi must be a positive uint32")
    payload_bytes = metadata.get("payload_bytes")
    if (
        isinstance(payload_bytes, bool)
        or not isinstance(payload_bytes, int)
        or payload_bytes != len(payload)
    ):
        raise PersonalBundleError("payload_bytes does not match app.wasm")
    digest = metadata.get("payload_sha256")
    if not isinstance(digest, str):
        raise PersonalBundleError("payload_sha256 must be a string")
    _validate_digest("payload_sha256", digest)
    if not hmac.compare_digest(digest, hashlib.sha256(payload).hexdigest()):
        raise PersonalBundleError("payload_sha256 does not match app.wasm")


def _canonical_json(value: Mapping[str, Any]) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _read_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PersonalBundleError(f"cannot read verified manifest: {error}") from error
    if not isinstance(value, dict):
        raise PersonalBundleError("verified manifest must be a JSON object")
    return value


def _decode_json_object(payload: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PersonalBundleError(f"cannot read {label}: {error}") from error
    if not isinstance(value, dict):
        raise PersonalBundleError(f"{label} must contain a JSON object")
    return value


def _validate_identifier(label: str, value: str) -> None:
    if _IDENTIFIER_PATTERN.fullmatch(value) is None:
        raise PersonalBundleError(f"{label} has an invalid v1 identifier")


def _validate_app_id(value: str) -> None:
    if len(value) > 64 or _APP_ID_PATTERN.fullmatch(value) is None:
        raise PersonalBundleError("app_id is not a bounded reverse-domain identifier")


def _validate_version(value: str) -> None:
    if len(value) > 64 or _VERSION_PATTERN.fullmatch(value) is None:
        raise PersonalBundleError("semantic_version is not supported by bundle v1")


def _validate_digest(label: str, value: str) -> None:
    if _DIGEST_PATTERN.fullmatch(value) is None:
        raise PersonalBundleError(f"{label} must be lowercase SHA-256 hex")


def _validate_hmac_key(value: bytes) -> None:
    if not isinstance(value, bytes) or len(value) != 32:
        raise PersonalBundleError("personal HMAC key must contain exactly 32 bytes")


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(128 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _fsync_directory(path: Path) -> None:
    try:
        descriptor = os.open(path, os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
