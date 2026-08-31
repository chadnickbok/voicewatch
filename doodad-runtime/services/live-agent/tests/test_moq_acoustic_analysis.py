"""Synthetic-only checks for the optional physical bench's numeric diagnostics."""
import importlib.util
from pathlib import Path

import numpy as np
import pytest

spec = importlib.util.spec_from_file_location('moq_acoustic_analysis',
    Path(__file__).resolve().parents[1] / 'tools/moq_acoustic_analysis.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def test_known_delay_gain_and_silence_are_distinguished():
    rng = np.random.default_rng(19)
    fixture = rng.integers(-5000, 5000, 16000, dtype=np.int16)
    capture = np.concatenate((np.zeros(1234, dtype=np.int16), fixture // 2,
                              np.zeros(2345, dtype=np.int16)))
    result = analysis.analyze(capture.tobytes(), fixture.tobytes())
    assert result['waveform'] == {'correlation': 1.0, 'offset_samples': 1234}
    silent = analysis.analyze(np.zeros(20000, dtype=np.int16).tobytes(), fixture.tobytes())
    assert silent['waveform']['correlation'] == 0
    assert silent['microphone']['rms'] == 0


def test_envelope_offset_is_in_pcm_samples_not_window_indices():
    # An aperiodic amplitude envelope with an integral 10 ms window shift.
    rng = np.random.default_rng(23)
    amplitude = np.repeat(rng.integers(1, 20, 100), 160)
    carrier = np.tile(np.array([-100, 100]), 8000)
    fixture = (amplitude * carrier).astype('<i2')
    capture = np.concatenate((np.zeros(1120, dtype='<i2'), fixture,
                              np.zeros(1600, dtype='<i2')))
    result = analysis.analyze(capture.tobytes(), fixture.tobytes())
    assert result['envelope'] == {'correlation': 1.0, 'offset_samples': 1120}


@pytest.mark.parametrize('size', [0, 639, 641, analysis.MAX_SAMPLES * 2 + 2])
def test_pcm_bounds_fail_before_analysis(size):
    with pytest.raises(ValueError, match='PCM bounds'):
        analysis.analyze(b'\0' * size, b'\0' * 640)
