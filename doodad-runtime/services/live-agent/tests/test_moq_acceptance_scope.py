"""The ten-minute replacement check must never silently inject network faults."""
import importlib.util
from pathlib import Path
import sys

import pytest


@pytest.mark.parametrize('extra', [
    ['--capture-rounds', '1'], ['--loss-percent', '1'],
    ['--added-rtt-ms', '30'], ['--packet-reorder-ms', '80'],
    ['--packet-duplicate-every', '7'], ['--capture-outage-ms', '500'],
    ['--playout-stall-ms', '350'], ['--group-delay-ms', '250'],
])
def test_acceptance_rejects_impairment_before_hardware(tmp_path, monkeypatch, extra):
    path = Path(__file__).parents[1] / 'tools/moq_provider_bench.py'
    monkeypatch.syspath_prepend(str(path.parent))
    spec = importlib.util.spec_from_file_location('acceptance_scope_bench', path)
    bench = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bench)
    output = tmp_path / 'must-not-exist'
    monkeypatch.setattr(sys, 'argv', ['bench', '--output', str(output), '--port', 'no-device',
        '--host', '127.0.0.1', '--idf-python', 'no-python', '--session-seconds', '600',
        '--capture-rounds', '3', *extra])
    with pytest.raises(SystemExit) as caught:
        bench.main()
    assert caught.value.code == 2
    assert not output.exists()
