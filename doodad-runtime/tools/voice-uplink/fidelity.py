"""Dependency-free signal analysis for Echo Bridge playback captures."""

from __future__ import annotations

import array
import math
import re
import statistics
import sys
import wave
from pathlib import Path
from typing import Sequence


PLAYBACK_TELEMETRY_PREFIX = "downlink playback stopped "


def parse_playback_telemetry(line: str) -> dict[str, int] | None:
    """Parse the firmware's final downlink playback counter line."""
    marker = line.find(PLAYBACK_TELEMETRY_PREFIX)
    if marker < 0:
        return None
    payload = line[marker + len(PLAYBACK_TELEMETRY_PREFIX):]
    values = {
        key: int(value)
        for key, value in re.findall(r"([a-z_]+)=(-?\d+)", payload)
    }
    required = {"received", "dropped", "underflow", "speaker_fail"}
    return values if required.issubset(values) else None


def expand_inter_run_gaps(specification: str, runs: int) -> list[float]:
    """Expand one repeated delay or validate one delay per lab run."""
    try:
        values = [float(value.strip()) for value in specification.split(",")]
    except ValueError as error:
        raise ValueError("inter-run gaps must be comma-separated seconds") from error
    if not values or any(value < 0 or value > 60 for value in values):
        raise ValueError("inter-run gaps must be between 0 and 60 seconds")
    if len(values) == 1:
        return values * runs
    if len(values) != runs:
        raise ValueError("inter-run gaps must contain one value or one value per run")
    return values


def make_tone_pcm(
    frequency_hz: float,
    duration_ms: int,
    *,
    sample_rate: int = 8_000,
    amplitude: int = 8_000,
    ramp_ms: int = 20,
) -> array.array[int]:
    """Return a deterministic signed-16 tone with click-suppressing ramps."""
    sample_count = sample_rate * duration_ms // 1_000
    ramp_samples = max(1, sample_rate * ramp_ms // 1_000)
    samples = array.array("h")
    for index in range(sample_count):
        ramp = min(
            1.0,
            index / ramp_samples,
            (sample_count - index - 1) / ramp_samples,
        )
        samples.append(int(
            amplitude
            * max(0.0, ramp)
            * math.sin(2 * math.pi * frequency_hz * index / sample_rate)
        ))
    return samples


def packet_timing_metrics(
    packet_times: Sequence[float],
    expected_duration_ms: float,
    *,
    packet_duration_ms: float = 20.0,
    minimum_interval_ms: float = 10.0,
) -> dict[str, object]:
    """Measure wall-clock packet pacing and evaluate the transport gates."""
    intervals = [
        (current - previous) * 1_000
        for previous, current in zip(packet_times, packet_times[1:])
    ]
    observed_duration_ms = (
        (packet_times[-1] - packet_times[0]) * 1_000 + packet_duration_ms
        if packet_times
        else 0.0
    )
    duration_error = (
        abs(observed_duration_ms - expected_duration_ms) / expected_duration_ms
        if expected_duration_ms > 0
        else 0.0
    )
    intervals_below_minimum = sum(
        interval < minimum_interval_ms for interval in intervals
    )
    gates = {
        "packet_interval": bool(packet_times) and intervals_below_minimum == 0,
        "duration": bool(packet_times) and duration_error <= 0.10,
    }
    return {
        "packet_count": len(packet_times),
        "expected_duration_ms": round(expected_duration_ms, 3),
        "observed_duration_ms": round(observed_duration_ms, 3),
        "duration_error_ratio": round(duration_error, 6),
        "minimum_interval_ms": minimum_interval_ms,
        "min_packet_interval_ms": round(min(intervals), 3) if intervals else None,
        "median_packet_interval_ms": (
            round(statistics.median(intervals), 3) if intervals else None
        ),
        "max_packet_interval_ms": round(max(intervals), 3) if intervals else None,
        "intervals_below_minimum": intervals_below_minimum,
        "gates": gates,
        "passed": all(gates.values()),
    }


def read_wave_mono_s16(path: Path) -> tuple[int, array.array[int]]:
    """Read PCM16 WAV data and downmix interleaved channels when necessary."""
    with wave.open(str(path), "rb") as reader:
        channels = reader.getnchannels()
        sample_width = reader.getsampwidth()
        sample_rate = reader.getframerate()
        raw = reader.readframes(reader.getnframes())
    if sample_width != 2:
        raise ValueError(f"expected 16-bit PCM WAV, got {sample_width * 8}-bit")
    if channels < 1:
        raise ValueError("WAV has no audio channels")
    interleaved = array.array("h")
    interleaved.frombytes(raw)
    if sys.byteorder != "little":
        interleaved.byteswap()
    if channels == 1:
        return sample_rate, interleaved
    mono = array.array("h")
    for offset in range(0, len(interleaved), channels):
        frame = interleaved[offset:offset + channels]
        mono.append(round(sum(frame) / len(frame)))
    return sample_rate, mono


def write_wave_mono_s16(
    path: Path,
    sample_rate: int,
    samples: Sequence[int],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    output = array.array("h", samples)
    if sys.byteorder != "little":
        output.byteswap()
    with wave.open(str(path), "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(sample_rate)
        writer.writeframes(output.tobytes())


def _rms(samples: Sequence[int]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(float(sample) * sample for sample in samples) / len(samples))


def _tone_energy_ratio(
    samples: Sequence[int], sample_rate: int, frequency_hz: float
) -> float:
    if not samples:
        return 0.0
    mean = sum(samples) / len(samples)
    cosine = 0.0
    sine = 0.0
    energy = 0.0
    angular = 2 * math.pi * frequency_hz / sample_rate
    for index, raw_sample in enumerate(samples):
        sample = raw_sample - mean
        cosine += sample * math.cos(angular * index)
        sine += sample * math.sin(angular * index)
        energy += sample * sample
    if energy <= 0:
        return 0.0
    explained = 2 * (cosine * cosine + sine * sine) / len(samples)
    return min(1.0, explained / energy)


def _goertzel_power(
    samples: Sequence[int], sample_rate: int, frequency_hz: float
) -> float:
    coefficient = 2 * math.cos(2 * math.pi * frequency_hz / sample_rate)
    previous = 0.0
    previous_previous = 0.0
    for sample in samples:
        current = sample + coefficient * previous - previous_previous
        previous_previous = previous
        previous = current
    return (
        previous_previous * previous_previous
        + previous * previous
        - coefficient * previous * previous_previous
    )


def _longest_contiguous(indices: Sequence[int]) -> tuple[int, int] | None:
    if not indices:
        return None
    best_start = best_end = current_start = previous = indices[0]
    for index in indices[1:]:
        if index != previous + 1:
            if previous - current_start > best_end - best_start:
                best_start, best_end = current_start, previous
            current_start = index
        previous = index
    if previous - current_start > best_end - best_start:
        best_start, best_end = current_start, previous
    return best_start, best_end


def _measure_tone(
    windows: Sequence[Sequence[int]],
    levels: Sequence[float],
    *,
    threshold: float,
    sample_rate: int,
    window_samples: int,
    hop_samples: int,
    expected_frequency_hz: float,
    expected_duration_ms: float,
    minimum_window_index: int = 0,
) -> tuple[dict[str, object], tuple[int, int] | None]:
    tone_indices: list[int] = []
    coherent_strengths: dict[int, float] = {}
    for index in range(minimum_window_index, len(windows)):
        ratio = _tone_energy_ratio(
            windows[index], sample_rate, expected_frequency_hz
        )
        if levels[index] >= threshold and ratio >= 0.08:
            tone_indices.append(index)
            # RMS alone counts a quiet but frequency-coherent room echo as
            # part of the source marker. Estimate the coherent tone amplitude
            # so the marker's reverberant tail can be trimmed independently of
            # broadband microphone noise.
            coherent_strengths[index] = levels[index] * math.sqrt(ratio)
    tone_block = _longest_contiguous(tone_indices)
    if tone_block is not None:
        first, last = tone_block
        peak_strength = max(
            coherent_strengths[index] for index in range(first, last + 1)
        )
        core_indices = [
            index
            for index in range(first, last + 1)
            if coherent_strengths[index] >= peak_strength * 0.5
        ]
        tone_block = _longest_contiguous(core_indices)
    frequency_hz: float | None = None
    duration_ms = 0.0
    start_ms: float | None = None
    end_ms: float | None = None
    if tone_block is not None:
        first, last = tone_block
        start_sample = first * hop_samples
        end_sample = min(
            (len(windows) - 1) * hop_samples + window_samples,
            last * hop_samples + window_samples,
        )
        start_ms = start_sample * 1_000 / sample_rate
        end_ms = end_sample * 1_000 / sample_rate
        duration_ms = end_ms - start_ms
        # Reject speech formants and transients; calibration markers are
        # deliberately continuous, so at least half of the expected marker
        # must be present before it can establish a timing endpoint.
        if duration_ms < max(100.0, expected_duration_ms * 0.5):
            tone_block = None
            start_ms = end_ms = None
            duration_ms = 0.0
        else:
            inset = min(
                sample_rate * 40 // 1_000,
                (end_sample - start_sample) // 4,
            )
            # Reconstruct the contiguous region from overlapping analysis
            # windows without requiring NumPy.
            tone_samples = list(windows[first])
            for index in range(first + 1, last + 1):
                tone_samples.extend(windows[index][-hop_samples:])
            if inset and len(tone_samples) > inset * 2:
                tone_samples = tone_samples[inset:-inset]
            if tone_samples:
                lower = max(1, round(expected_frequency_hz - 100))
                upper = round(expected_frequency_hz + 100)
                frequency_hz = float(max(
                    range(lower, upper + 1),
                    key=lambda frequency: _goertzel_power(
                        tone_samples, sample_rate, frequency
                    ),
                ))
    return ({
        "detected": tone_block is not None,
        "expected_frequency_hz": expected_frequency_hz,
        "frequency_hz": frequency_hz,
        "expected_duration_ms": expected_duration_ms,
        "duration_ms": round(duration_ms, 3),
        "start_ms": round(start_ms, 3) if start_ms is not None else None,
        "end_ms": round(end_ms, 3) if end_ms is not None else None,
    }, tone_block)


def analyze_capture_samples(
    samples: Sequence[int],
    sample_rate: int,
    *,
    expected_tone_hz: float,
    expected_tone_duration_ms: float,
    expected_closing_tone_hz: float,
    expected_closing_tone_duration_ms: float,
    expected_marker_offset_ms: float,
    expected_program_duration_ms: float,
) -> dict[str, object]:
    """Measure playback using opening and closing calibration markers."""
    window_samples = max(1, sample_rate * 40 // 1_000)
    hop_samples = max(1, sample_rate * 20 // 1_000)
    windows = [
        samples[start:start + window_samples]
        for start in range(0, max(0, len(samples) - window_samples + 1), hop_samples)
    ]
    levels = [_rms(window) for window in windows]
    if levels:
        noise_probe = levels[:max(1, min(len(levels), 15))]
        threshold = max(
            32.0,
            statistics.median(noise_probe) * 1.5,
            max(levels) * 0.02,
        )
    else:
        threshold = 64.0

    active_indices = [
        index for index, level in enumerate(levels) if level >= threshold
    ]
    if active_indices:
        active_start_sample = active_indices[0] * hop_samples
        active_end_sample = min(
            len(samples), active_indices[-1] * hop_samples + window_samples
        )
        active_duration_ms = (
            (active_end_sample - active_start_sample) * 1_000 / sample_rate
        )
    else:
        active_start_sample = active_end_sample = 0
        active_duration_ms = 0.0

    tone, tone_block = _measure_tone(
        windows,
        levels,
        threshold=threshold,
        sample_rate=sample_rate,
        window_samples=window_samples,
        hop_samples=hop_samples,
        expected_frequency_hz=expected_tone_hz,
        expected_duration_ms=expected_tone_duration_ms,
    )
    closing_minimum = tone_block[1] + 1 if tone_block is not None else 0
    closing_tone, _ = _measure_tone(
        windows,
        levels,
        threshold=threshold,
        sample_rate=sample_rate,
        window_samples=window_samples,
        hop_samples=hop_samples,
        expected_frequency_hz=expected_closing_tone_hz,
        expected_duration_ms=expected_closing_tone_duration_ms,
        minimum_window_index=closing_minimum,
    )

    frequency_gate = (
        tone["frequency_hz"] is not None
        and abs(float(tone["frequency_hz"]) - expected_tone_hz) <= 20.0
    )
    tone_duration_gate = (
        bool(tone["detected"])
        and abs(float(tone["duration_ms"]) - expected_tone_duration_ms)
        <= expected_tone_duration_ms * 0.10
    )
    closing_frequency_gate = (
        closing_tone["frequency_hz"] is not None
        and abs(
            float(closing_tone["frequency_hz"]) - expected_closing_tone_hz
        ) <= 20.0
    )
    marker_offset_ms: float | None = None
    marker_error_ratio: float | None = None
    if isinstance(tone["start_ms"], (int, float)) and isinstance(
        closing_tone["start_ms"], (int, float)
    ):
        marker_offset_ms = float(closing_tone["start_ms"]) - float(tone["start_ms"])
        marker_error_ratio = (
            abs(marker_offset_ms - expected_marker_offset_ms)
            / expected_marker_offset_ms
        )
    program_duration_gate = (
        marker_error_ratio is not None and marker_error_ratio <= 0.10
    )
    gates = {
        "tone_frequency": frequency_gate,
        "tone_duration": tone_duration_gate,
        "closing_tone_frequency": closing_frequency_gate,
        "program_duration": program_duration_gate,
    }
    return {
        "sample_rate": sample_rate,
        "samples": len(samples),
        "file_duration_ms": round(len(samples) * 1_000 / sample_rate, 3),
        "analysis_threshold_rms": round(threshold, 3),
        "active_start_ms": round(active_start_sample * 1_000 / sample_rate, 3),
        "active_end_ms": round(active_end_sample * 1_000 / sample_rate, 3),
        "active_duration_ms": round(active_duration_ms, 3),
        "expected_program_duration_ms": round(expected_program_duration_ms, 3),
        "tone": tone,
        "closing_tone": closing_tone,
        "marker_offset": {
            "expected_ms": round(expected_marker_offset_ms, 3),
            "observed_ms": (
                round(marker_offset_ms, 3) if marker_offset_ms is not None else None
            ),
            "error_ratio": (
                round(marker_error_ratio, 6)
                if marker_error_ratio is not None
                else None
            ),
        },
        "gates": gates,
        "passed": all(gates.values()),
    }


def analyze_capture_wave(
    path: Path,
    *,
    expected_tone_hz: float,
    expected_tone_duration_ms: float,
    expected_closing_tone_hz: float,
    expected_closing_tone_duration_ms: float,
    expected_marker_offset_ms: float,
    expected_program_duration_ms: float,
) -> dict[str, object]:
    sample_rate, samples = read_wave_mono_s16(path)
    return analyze_capture_samples(
        samples,
        sample_rate,
        expected_tone_hz=expected_tone_hz,
        expected_tone_duration_ms=expected_tone_duration_ms,
        expected_closing_tone_hz=expected_closing_tone_hz,
        expected_closing_tone_duration_ms=expected_closing_tone_duration_ms,
        expected_marker_offset_ms=expected_marker_offset_ms,
        expected_program_duration_ms=expected_program_duration_ms,
    )


def extract_wave_segment(
    source: Path,
    destination: Path,
    *,
    start_ms: float,
    duration_ms: float,
) -> None:
    sample_rate, samples = read_wave_mono_s16(source)
    start = max(0, round(start_ms * sample_rate / 1_000))
    end = min(len(samples), start + round(duration_ms * sample_rate / 1_000))
    write_wave_mono_s16(destination, sample_rate, samples[start:end])
