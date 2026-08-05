from __future__ import annotations

import hashlib
import hmac
import json
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest

import doodad_agent.app_verifier as app_verifier_module
from doodad_agent.app_verifier import (
    RestTimerVerifier,
    VerificationError,
    VerifiedArtifact,
    package_tree_snapshot,
)
from doodad_agent.personal_bundle import (
    BUNDLE_HEADER,
    BUNDLE_HMAC_DOMAIN,
    BUNDLE_MAGIC,
    ArtifactStore,
    PackagedArtifact,
    PersonalBundleError,
    PersonalBundlePackager,
    PersonalTrustProfile,
    decode_personal_bundle,
    encode_personal_bundle,
)


KEY = bytes(range(32))
RUNTIME_ROOT = Path(__file__).resolve().parents[3]


def metadata(payload: bytes = b"\0asm-test") -> dict[str, object]:
    return {
        "bundle_version": 1,
        "kind": "personal",
        "owner_id": "nick.local",
        "signer_key_id": "macbook-v0",
        "app_id": "dev.doodad.generated-rest",
        "name": "Lift Rest",
        "semantic_version": "0.1.0",
        "host_abi": 1,
        "identity": {"icon": "timer", "theme_seed": "#20BFF4"},
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "payload_bytes": len(payload),
    }


def verified_package(tmp_path: Path, payload: bytes = b"\0asm-app") -> VerifiedArtifact:
    package = tmp_path / "mutable-codex-workspace" / "package"
    package.mkdir(parents=True)
    (package / "manifest.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "id": "dev.doodad.generated-rest",
                "name": "Lift Rest",
                "version": "0.1.0",
                "host_abi": 1,
                "identity": {"icon": "timer", "theme_seed": "#20BFF4"},
                "capabilities": ["ui.mount", "timer.schedule"],
                "wasm": "app.wasm",
            }
        ),
        encoding="utf-8",
    )
    (package / "app.wasm").write_bytes(payload)
    (package / "preview.bmp").write_bytes(b"mutable review evidence")
    tree_sha256, _ = package_tree_snapshot(package)
    return VerifiedArtifact(
        "dev.doodad.generated-rest@0.1.0",
        str(package),
        str(package / "preview.bmp"),
        tree_sha256,
        "verified",
        ("schema", "build"),
    )


def test_ddb1_wire_format_is_exact_canonical_and_round_trips() -> None:
    payload = b"\0asm-test"
    document = metadata(payload)
    bundle = encode_personal_bundle(document, payload, KEY)
    canonical = json.dumps(
        document, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode()
    header = struct.pack(">4sII", BUNDLE_MAGIC, len(canonical), len(payload))
    assert bundle[: BUNDLE_HEADER.size] == header
    assert bundle[BUNDLE_HEADER.size : BUNDLE_HEADER.size + len(canonical)] == canonical
    assert bundle[-32:] == hmac.digest(
        KEY, BUNDLE_HMAC_DOMAIN + header + canonical + payload, "sha256"
    )
    # Shared with the firmware verifier as the cross-language DDB1 vector.
    assert bundle[-32:].hex() == (
        "f7507d730c6ff6f3e0a693ddb25b116563a9b38ebe5e831cfb49be73998f5e71"
    )
    assert hashlib.sha256(bundle).hexdigest() == (
            "ffb5818c5452b80be1c01c65e1413b53a481d18fb3681603626c70cfa2ec8320"
    )
    decoded, decoded_payload = decode_personal_bundle(bundle, KEY)
    assert decoded == document
    assert decoded_payload == payload


def test_personal_bundle_uses_runtime_app_id_and_payload_bounds() -> None:
    maximum_id = "a." + "b" * 62
    maximum_id_metadata = metadata()
    maximum_id_metadata["app_id"] = maximum_id
    encode_personal_bundle(maximum_id_metadata, b"\0asm-test", KEY)

    oversized_id_metadata = dict(maximum_id_metadata)
    oversized_id_metadata["app_id"] = maximum_id + "b"
    with pytest.raises(PersonalBundleError, match="bounded reverse-domain"):
        encode_personal_bundle(oversized_id_metadata, b"\0asm-test", KEY)

    maximum_payload = b"x" * (1024 * 1024)
    encode_personal_bundle(metadata(maximum_payload), maximum_payload, KEY)
    oversized_payload = maximum_payload + b"x"
    with pytest.raises(PersonalBundleError, match="payload length"):
        encode_personal_bundle(metadata(oversized_payload), oversized_payload, KEY)


def test_verifier_distinguishes_ui_namespace_from_signed_package_identity() -> None:
    manifest = {"id": "dev.doodad.timer", "version": "1.0.0", "host_abi": 1}
    appspec = {"app_id": "timer"}
    agent = {
        "app_id": "dev.doodad.timer",
        "app_version": "1.0.0",
        "host_abi": 1,
    }
    RestTimerVerifier._validate_identity(manifest, appspec, agent)
    agent["app_id"] = "dev.doodad.impostor"
    with pytest.raises(VerificationError, match="agent contract app_id"):
        RestTimerVerifier._validate_identity(manifest, appspec, agent)


@pytest.mark.parametrize("offset", [12, -33, -1])
def test_ddb1_rejects_metadata_payload_or_tag_tampering(offset: int) -> None:
    bundle = bytearray(encode_personal_bundle(metadata(), b"\0asm-test", KEY))
    bundle[offset] ^= 1
    with pytest.raises(PersonalBundleError):
        decode_personal_bundle(bytes(bundle), KEY)


def test_ddb1_rejects_wrong_key_trailing_bytes_and_noncanonical_json() -> None:
    payload = b"\0asm-test"
    bundle = encode_personal_bundle(metadata(payload), payload, KEY)
    with pytest.raises(PersonalBundleError, match="signature"):
        decode_personal_bundle(bundle, b"x" * 32)
    with pytest.raises(PersonalBundleError, match="length"):
        decode_personal_bundle(bundle + b"extra", KEY)

    pretty = json.dumps(metadata(payload), indent=2).encode()
    header = BUNDLE_HEADER.pack(BUNDLE_MAGIC, len(pretty), len(payload))
    signed = header + pretty + payload
    noncanonical = signed + hmac.digest(
        KEY, BUNDLE_HMAC_DOMAIN + signed, "sha256"
    )
    with pytest.raises(PersonalBundleError, match="canonical"):
        decode_personal_bundle(noncanonical, KEY)


def test_metadata_binds_owner_identity_and_exact_payload() -> None:
    payload = b"\0asm-test"
    wrong_size = metadata(payload)
    wrong_size["payload_bytes"] = len(payload) + 1
    with pytest.raises(PersonalBundleError, match="payload_bytes"):
        encode_personal_bundle(wrong_size, payload, KEY)

    for invalid_version in (True, 1.0):
        wrong_bundle_version = metadata(payload)
        wrong_bundle_version["bundle_version"] = invalid_version
        with pytest.raises(PersonalBundleError, match="unsupported identity"):
            encode_personal_bundle(wrong_bundle_version, payload, KEY)

    float_size = metadata(payload)
    float_size["payload_bytes"] = float(len(payload))
    with pytest.raises(PersonalBundleError, match="payload_bytes"):
        encode_personal_bundle(float_size, payload, KEY)

    # The strict decoder must also reject a correctly authenticated JSON number
    # that Python would otherwise compare equal to the required integer.
    float_document = metadata(payload)
    float_document["bundle_version"] = 1.0
    canonical = json.dumps(
        float_document, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode()
    header = BUNDLE_HEADER.pack(BUNDLE_MAGIC, len(canonical), len(payload))
    signed = header + canonical + payload
    float_bundle = signed + hmac.digest(KEY, BUNDLE_HMAC_DOMAIN + signed, "sha256")
    with pytest.raises(PersonalBundleError, match="unsupported identity"):
        decode_personal_bundle(float_bundle, KEY)

    wrong_digest = metadata(payload)
    wrong_digest["payload_sha256"] = "0" * 64
    with pytest.raises(PersonalBundleError, match="does not match"):
        encode_personal_bundle(wrong_digest, payload, KEY)

    extra = metadata(payload)
    extra["capabilities"] = []
    with pytest.raises(PersonalBundleError, match="keys do not match"):
        encode_personal_bundle(extra, payload, KEY)

    invalid_app_id = metadata(payload)
    invalid_app_id["app_id"] = "dev.doodad.generated_rest"
    with pytest.raises(PersonalBundleError, match="reverse-domain"):
        encode_personal_bundle(invalid_app_id, payload, KEY)

    invalid_name = metadata(payload)
    invalid_name["name"] = "Lift\0Rest"
    with pytest.raises(PersonalBundleError, match="printable Unicode"):
        encode_personal_bundle(invalid_name, payload, KEY)


@pytest.mark.parametrize(
    ("identity", "message"),
    [
        ({"icon": "downloaded_svg", "theme_seed": "#20BFF4"}, "curated"),
        ({"icon": "water_drop", "theme_seed": "#20bff4"}, "uppercase"),
    ],
)
def test_signed_visual_identity_is_strict(identity: dict[str, str], message: str) -> None:
    document = metadata()
    document["identity"] = identity
    with pytest.raises(PersonalBundleError, match=message):
        encode_personal_bundle(document, b"\0asm-test", KEY)


def test_packager_promotes_only_wasm_to_immutable_content_addressed_store(
    tmp_path: Path,
) -> None:
    verified = verified_package(tmp_path)
    profile = PersonalTrustProfile("nick.local", "macbook-v0", KEY)
    store = ArtifactStore(tmp_path / "durable-artifacts")
    packager = PersonalBundlePackager(profile, store)

    first = packager.package(verified)
    second = packager.package(verified)
    assert first == second
    assert first.generation_id == (
        f"dev.doodad.generated-rest@0.1.0+{first.payload_sha256}"
    )
    assert Path(first.storage_path).is_relative_to(store.root)
    assert not Path(first.storage_path).is_relative_to(
        Path(verified.package_path).parent
    )
    stored = Path(first.storage_path).read_bytes()
    assert hashlib.sha256(stored).hexdigest() == first.bundle_sha256
    decoded, wasm = decode_personal_bundle(stored, KEY)
    assert decoded["owner_id"] == "nick.local"
    assert decoded["app_id"] == "dev.doodad.generated-rest"
    assert wasm == b"\0asm-app"
    assert b"mutable review evidence" not in stored

    Path(verified.preview_path).write_bytes(b"changed after verification")
    with pytest.raises(PersonalBundleError, match="changed before outer packaging"):
        packager.package(verified)


def test_payload_change_requires_reverification_and_never_overwrites_old_object(
    tmp_path: Path,
) -> None:
    verified = verified_package(tmp_path, b"\0asm-v1")
    store = ArtifactStore(tmp_path / "artifacts")
    packager = PersonalBundlePackager(
        PersonalTrustProfile("nick.local", "macbook-v0", KEY), store
    )
    first = packager.package(verified)
    first_bytes = Path(first.storage_path).read_bytes()
    (Path(verified.package_path) / "app.wasm").write_bytes(b"\0asm-v2")
    with pytest.raises(PersonalBundleError, match="changed before outer packaging"):
        packager.package(verified)
    package = Path(verified.package_path)
    tree_sha256, _ = package_tree_snapshot(package)
    reverified = VerifiedArtifact(
        verified.artifact_id,
        verified.package_path,
        verified.preview_path,
        tree_sha256,
        verified.summary,
        verified.gates,
    )
    second = packager.package(reverified)
    assert second.payload_sha256 != first.payload_sha256
    assert second.bundle_sha256 != first.bundle_sha256
    assert Path(first.storage_path).read_bytes() == first_bytes


def test_corrupt_content_addressed_object_is_not_served_or_silently_replaced(
    tmp_path: Path,
) -> None:
    store = ArtifactStore(tmp_path / "artifacts")
    digest, path = store.put(b"signed bundle")
    path.write_bytes(b"corrupt")
    with pytest.raises(PersonalBundleError, match="digest check"):
        store.resolve(digest)
    with pytest.raises(PersonalBundleError, match="corrupt"):
        store.put(b"signed bundle")


def test_artifact_document_round_trip_and_profile_validation(tmp_path: Path) -> None:
    artifact = PersonalBundlePackager(
        PersonalTrustProfile.from_hex("nick.local", "macbook-v0", KEY.hex()),
        ArtifactStore(tmp_path / "artifacts"),
    ).package(verified_package(tmp_path))
    assert PackagedArtifact.from_document(artifact.document()) == artifact
    with pytest.raises(PersonalBundleError, match="exactly 32"):
        PersonalTrustProfile("nick.local", "macbook-v0", b"short")
    with pytest.raises(PersonalBundleError, match="exactly 32"):
        PersonalTrustProfile("nick.local", "macbook-v0", b"x" * 33)
    with pytest.raises(PersonalBundleError, match="identifier"):
        PersonalTrustProfile("Nick has spaces", "macbook-v0", KEY)


def test_packager_rejects_identity_mismatch_and_storage_inside_workspace(
    tmp_path: Path,
) -> None:
    verified = verified_package(tmp_path)
    profile = PersonalTrustProfile("nick.local", "macbook-v0", KEY)
    mismatched = VerifiedArtifact(
        "dev.doodad.somewhere-else@0.1.0",
        verified.package_path,
        verified.preview_path,
        verified.sha256,
        verified.summary,
        verified.gates,
    )
    with pytest.raises(PersonalBundleError, match="identity"):
        PersonalBundlePackager(
            profile, ArtifactStore(tmp_path / "separate-artifacts")
        ).package(mismatched)
    with pytest.raises(PersonalBundleError, match="must be separate"):
        PersonalBundlePackager(
            profile, ArtifactStore(Path(verified.package_path) / "artifacts")
        ).package(verified)


def test_independent_build_commands_never_inherit_outer_packager_key(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    captured: dict[str, str] = {}

    def fake_run(*_args, **kwargs):  # type: ignore[no-untyped-def]
        captured.update(kwargs["env"])
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    monkeypatch.setenv("DOODAD_PERSONAL_HMAC_KEY_HEX", KEY.hex())
    monkeypatch.setattr(app_verifier_module.subprocess, "run", fake_run)
    app_verifier_module.RestTimerVerifier(RUNTIME_ROOT)._run(["doodad", "check"])
    assert "DOODAD_PERSONAL_HMAC_KEY_HEX" not in captured
    assert captured["ASDF_RUST_VERSION"] == "1.95.0"
