"""Packaging uses only generated fixtures and never controls launchd."""
import fcntl
import hashlib
import json
from pathlib import Path

import pytest

from doodad_agent.moq_deploy import LICENSES, prepare
from doodad_agent.moq_supervisor import SupervisorError, load_profile
from test_moq_supervisor import fixture_profile


def inputs(tmp_path):
    source = tmp_path/'source'; source.mkdir(mode=0o700)
    profile = fixture_profile(source)
    licenses = source/'licenses'; licenses.mkdir()
    for name in LICENSES: (licenses/name).write_text('Test notice\n')
    output = tmp_path/'deployed'/'supervisor.json'
    return profile, licenses, output


def test_copies_binary_trust_notices_and_preserves_prior_generation(tmp_path):
    profile, licenses, output = inputs(tmp_path)
    first = prepare(profile, output, licenses)
    original = output.read_bytes()
    deployed, host = load_profile(output)
    assert Path(deployed['endpoint_binary']).parent == first
    for field in ('certificate', 'private_key', 'device_keys'):
        assert Path(host[field]).parent == first
        assert Path(host[field]).stat().st_mode & 0o077 == 0
    manifest = json.loads((first/'manifest.json').read_text())
    for name, expected in manifest.items():
        assert hashlib.sha256((first/name).read_bytes()).hexdigest() == expected
    assert all((first/'licenses'/name).read_text() == 'Test notice\n' for name in LICENSES)
    second = prepare(profile, output, licenses)
    assert second != first and first.exists() and second.exists()
    assert output.read_bytes() != original


def test_missing_notice_cannot_replace_previous_profile(tmp_path):
    profile, licenses, output = inputs(tmp_path)
    first = prepare(profile, output, licenses)
    original = output.read_bytes()
    (licenses/LICENSES[0]).unlink()
    with pytest.raises(FileNotFoundError): prepare(profile, output, licenses)
    assert output.read_bytes() == original
    assert list(output.parent.glob('generation-*')) == [first]


def test_cannot_replace_live_profile_or_accept_public_directory(tmp_path):
    profile, licenses, output = inputs(tmp_path)
    prepare(profile, output, licenses)
    original = output.read_bytes()
    with output.open('rb') as active:
        fcntl.flock(active, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with pytest.raises(BlockingIOError): prepare(profile, output, licenses)
    assert output.read_bytes() == original
    output.parent.chmod(0o755)
    with pytest.raises(SupervisorError): prepare(profile, output, licenses)


def test_bounded_wait_for_stopped_supervisor_preserves_exclusion(tmp_path):
    import threading
    profile, licenses, output = inputs(tmp_path)
    prepare(profile, output, licenses)
    with output.open('rb') as active:
        fcntl.flock(active, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with pytest.raises(BlockingIOError):
            prepare(profile, output, licenses, wait_unlocked=0.05)
        release = threading.Timer(0.05, lambda: fcntl.flock(active, fcntl.LOCK_UN))
        release.start()
        try:
            prepare(profile, output, licenses, wait_unlocked=1)
        finally:
            release.join()
