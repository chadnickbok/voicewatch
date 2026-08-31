"""Renewal must prove a still-live, attached owner; it cannot revive a grant."""
import pytest

from doodad_agent.moq_auth import AuthorizationError, GrantRegistry, bootstrap_proof, renewal_proof

DEVICE = 'ultra-renew-test'
KEY = bytes(range(32))


@pytest.fixture
def active():
    clock = [100.0, 1_800_000_000.0]
    registry = GrantRegistry({DEVICE: KEY}, lease_seconds=60,
                             monotonic=lambda: clock[0], wall_clock=lambda: clock[1])
    challenge = registry.challenge(DEVICE)
    grant = registry.issue(DEVICE, challenge, bootstrap_proof(KEY, DEVICE, challenge))
    control, media = object(), object()
    registry.activate_control(grant.control_token, control)
    registry.attach_media(grant.media_token, media)
    return registry, clock, grant, control, media


def advance(clock, seconds):
    clock[0] += seconds
    clock[1] += seconds


def test_renewal_keeps_identity_scope_owners_and_one_use_tokens(active):
    registry, clock, grant, control, media = active
    assert registry.renewal_challenge(grant.session_id, control) is None
    advance(clock, 30)
    nonce = registry.renewal_challenge(grant.session_id, control)
    assert nonce and registry.renewal_challenge(grant.session_id, control) is None
    result = registry.renew(grant.session_id, control, nonce,
                           renewal_proof(KEY, DEVICE, grant.session_id, nonce))
    assert result['revision'] == 1 and result['expires_unix'] == 1_800_000_090
    assert result['lease_seconds'] == 60 and result['time']['nonce'] == nonce
    advance(clock, 31)
    assert registry.identity(grant.session_id, control) == DEVICE
    assert registry.valid(grant.session_id, media)
    assert registry._grants[grant.session_id].publish == grant.publish
    assert registry._grants[grant.session_id].subscribe == grant.subscribe
    with pytest.raises(AuthorizationError):
        registry.activate_control(grant.control_token, object())
    with pytest.raises(AuthorizationError):
        registry.attach_media(grant.media_token, object())
    with pytest.raises(AuthorizationError):
        registry.renew(grant.session_id, control, nonce,
                       renewal_proof(KEY, DEVICE, grant.session_id, nonce))
    advance(clock, 29)
    assert not registry.valid(grant.session_id, media)


@pytest.mark.parametrize('fault', ['key', 'device', 'session', 'domain', 'owner', 'media-owner',
                                  'revoked', 'expired', 'timeout', 'wall-rollback', 'mono-rollback'])
def test_renewal_rejection_cannot_extend_or_revive(active, fault):
    registry, clock, grant, control, media = active
    advance(clock, 30)
    nonce = registry.renewal_challenge(grant.session_id, control)
    proof = renewal_proof(KEY, DEVICE, grant.session_id, nonce)
    owner = control
    if fault == 'key': proof = renewal_proof(bytes(reversed(KEY)), DEVICE, grant.session_id, nonce)
    if fault == 'device': proof = renewal_proof(KEY, 'another-watch', grant.session_id, nonce)
    if fault == 'session': proof = renewal_proof(KEY, DEVICE, '0'*32, nonce)
    if fault == 'domain': proof = bootstrap_proof(KEY, DEVICE, nonce)
    if fault == 'owner': owner = object()
    if fault == 'media-owner': owner = media
    if fault == 'revoked': registry.revoke(grant.session_id, control)
    if fault == 'expired': advance(clock, 30)
    if fault == 'timeout': advance(clock, 10)
    if fault == 'wall-rollback': clock[1] -= 2
    if fault == 'mono-rollback': clock[0] -= 1
    with pytest.raises(AuthorizationError):
        registry.renew(grant.session_id, owner, nonce, proof)
    if fault in {'key', 'device', 'session', 'domain'}:
        with pytest.raises(AuthorizationError):
            registry.renew(grant.session_id, control, nonce,
                           renewal_proof(KEY, DEVICE, grant.session_id, nonce))
    advance(clock, 61)
    assert not registry.valid(grant.session_id, media)


def test_replacement_and_unattached_session_cannot_renew(active):
    registry, clock, grant, control, media = active
    advance(clock, 30)
    nonce = registry.renewal_challenge(grant.session_id, control)
    challenge = registry.challenge(DEVICE)
    fresh = registry.issue(DEVICE, challenge, bootstrap_proof(KEY, DEVICE, challenge))
    replacement = object()
    registry.activate_control(fresh.control_token, replacement)
    with pytest.raises(AuthorizationError):
        registry.renew(grant.session_id, control, nonce, renewal_proof(KEY, DEVICE, grant.session_id, nonce))
    with pytest.raises(AuthorizationError):
        registry.renewal_challenge(fresh.session_id, replacement)
    assert not registry.valid(grant.session_id, media)
    assert registry.valid(fresh.session_id, replacement)
