"""Bounded, in-memory comparison of microphone PCM with a synthetic fixture.

No captured audio, spectrum or transcript is returned or written. The caller
must clear its capture buffer after analysis; these aggregate diagnostics are
not a speech-recognition acceptance test.
"""
import numpy as np

RATE = 16000
MAX_SAMPLES = 31 * RATE


def _match(signal, reference, stride=1):
    """Best absolute normalized correlation over fully overlapping windows."""
    n, m = len(signal), len(reference)
    if m < 2 or n < m:
        return {"correlation": 0.0, "offset_samples": 0}
    reference = reference - reference.mean()
    power = float(reference @ reference)
    if power <= 1e-12:
        return {"correlation": 0.0, "offset_samples": 0}
    size = 1 << (n + m - 2).bit_length()
    dots = np.fft.irfft(np.fft.rfft(signal, size) *
                        np.fft.rfft(reference[::-1], size), size)[m-1:n]
    sums = np.concatenate(([0.0], np.cumsum(signal)))
    squares = np.concatenate(([0.0], np.cumsum(signal * signal)))
    variance = np.maximum(0, squares[m:] - squares[:-m] -
                          (sums[m:] - sums[:-m]) ** 2 / m)
    scores = np.abs(dots) / np.sqrt(np.maximum(variance * power, 1e-12))
    index = int(np.argmax(scores))
    return {"correlation": round(min(1.0, float(scores[index])), 5),
            "offset_samples": index * stride}


def _levels(pcm):
    centered = pcm - pcm.mean()
    power = np.abs(np.fft.rfft(centered)) ** 2
    frequencies = np.fft.rfftfreq(len(pcm), 1 / RATE)
    total = max(float(power.sum()), 1e-12)
    bands = {}
    for low, high in ((0, 80), (80, 300), (300, 1000), (1000, 3000), (3000, 8001)):
        bands[f"{low}_{high}_hz_fraction"] = round(float(power[
            (frequencies >= low) & (frequencies < high)].sum()) / total, 5)
    return dict(dc=round(float(pcm.mean()), 2),
                rms=round(float(np.sqrt(np.mean(pcm * pcm))), 2),
                zero_crossing_fraction=round(float(np.mean(centered[:-1] * centered[1:] < 0)), 5),
                **bands)


def analyze(captured_pcm, fixture_pcm):
    """Return only numeric/fixed diagnostics; accept at most 31 seconds each."""
    for pcm in (captured_pcm, fixture_pcm):
        if not 640 <= len(pcm) <= MAX_SAMPLES * 2 or len(pcm) % 2:
            raise ValueError("acoustic analysis PCM bounds")
    captured = np.frombuffer(captured_pcm, dtype='<i2').astype(np.float64)
    fixture = np.frombuffer(fixture_pcm, dtype='<i2').astype(np.float64)
    try:
        # A 10 ms RMS envelope is more robust than waveform correlation to
        # loudspeaker/microphone frequency response and room reverberation.
        envelope = lambda x: np.sqrt(np.mean(x[:len(x)//160*160].reshape(-1, 160)**2, axis=1))
        result = dict(capture_samples=len(captured), fixture_samples=len(fixture),
                      microphone=_levels(captured), fixture=_levels(fixture),
                      waveform=_match(captured, fixture),
                      envelope=_match(envelope(captured), envelope(fixture), stride=160))
        # Gross sample-rate errors should be visible without modifying the
        # audio sent to the actual provider or biasing its transcription.
        result['duration_scale'] = {}
        for scale in (0.5, 0.75, 1.25, 1.5, 2.0):
            count = round(len(fixture) * scale)
            if count <= len(captured):
                reference = np.interp(np.arange(count) / scale,
                                      np.arange(len(fixture)), fixture)
                result['duration_scale'][str(scale)] = _match(envelope(captured), envelope(reference), stride=160)
                reference.fill(0)
        return result
    finally:
        captured.fill(0)
        fixture.fill(0)
