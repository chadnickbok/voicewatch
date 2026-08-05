from __future__ import annotations

import json

import pytest

from doodad_agent.main import check_config, personal_trust_from_environment
from doodad_agent.personal_bundle import PersonalBundleError


PERSONAL_ENV = (
    "DOODAD_PERSONAL_OWNER_ID",
    "DOODAD_PERSONAL_SIGNER_KEY_ID",
    "DOODAD_PERSONAL_HMAC_KEY_HEX",
)


def clear_personal_env(monkeypatch: pytest.MonkeyPatch) -> None:
    for name in PERSONAL_ENV:
        monkeypatch.delenv(name, raising=False)


def test_personal_delivery_is_disabled_when_owner_and_key_are_absent(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    clear_personal_env(monkeypatch)
    assert personal_trust_from_environment() is None


def test_explicit_personal_profile_uses_default_or_overridden_signer(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    clear_personal_env(monkeypatch)
    monkeypatch.setenv("DOODAD_PERSONAL_OWNER_ID", "nick.local")
    monkeypatch.setenv("DOODAD_PERSONAL_HMAC_KEY_HEX", bytes(range(32)).hex())
    profile = personal_trust_from_environment()
    assert profile is not None
    assert profile.owner_id == "nick.local"
    assert profile.signer_key_id == "personal-v1"
    assert profile.hmac_key == bytes(range(32))

    monkeypatch.setenv("DOODAD_PERSONAL_SIGNER_KEY_ID", "macbook-v0")
    profile = personal_trust_from_environment()
    assert profile is not None and profile.signer_key_id == "macbook-v0"


@pytest.mark.parametrize(
    ("owner", "key"),
    [
        ("nick.local", ""),
        ("", bytes(range(32)).hex()),
        ("nick.local", "not-hex"),
        ("nick.local", "00" * 8),
    ],
)
def test_partial_or_malformed_personal_profile_fails_clearly(
    monkeypatch: pytest.MonkeyPatch, owner: str, key: str
) -> None:
    clear_personal_env(monkeypatch)
    if owner:
        monkeypatch.setenv("DOODAD_PERSONAL_OWNER_ID", owner)
    if key:
        monkeypatch.setenv("DOODAD_PERSONAL_HMAC_KEY_HEX", key)
    with pytest.raises(PersonalBundleError):
        personal_trust_from_environment()


def test_check_config_reports_personal_delivery_without_exposing_key(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    for name in (
        "OPENAI_API_KEY",
        "ELEVENLABS_API_KEY",
        "ELEVENLABS_DEFAULT_VOICE_ID",
    ):
        monkeypatch.setenv(name, "configured")
    clear_personal_env(monkeypatch)
    monkeypatch.setenv("DOODAD_PERSONAL_OWNER_ID", "nick.local")
    secret = bytes(range(32)).hex()
    monkeypatch.setenv("DOODAD_PERSONAL_HMAC_KEY_HEX", secret)
    assert check_config() == 0
    output = capsys.readouterr().out
    result = json.loads(output)
    assert result["ready"] is True
    assert result["personal_app_delivery"] == {
        "enabled": True,
        "valid": True,
        "error": None,
    }
    assert secret not in output


def test_check_config_marks_partial_personal_profile_invalid(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    for name in (
        "OPENAI_API_KEY",
        "ELEVENLABS_API_KEY",
        "ELEVENLABS_DEFAULT_VOICE_ID",
    ):
        monkeypatch.setenv(name, "configured")
    clear_personal_env(monkeypatch)
    monkeypatch.setenv("DOODAD_PERSONAL_OWNER_ID", "nick.local")
    assert check_config() == 2
    result = json.loads(capsys.readouterr().out)
    assert result["ready"] is False
    assert result["personal_app_delivery"]["valid"] is False
    assert "configured together" in result["personal_app_delivery"]["error"]
