import array
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fidelity import (
    analyze_capture_samples,
    expand_inter_run_gaps,
    make_tone_pcm,
    packet_timing_metrics,
    parse_playback_telemetry,
)
from protocol import (
    capture_correlation,
    correlated_transcript,
    current_guest_capture_request,
    envelope,
    word_error_rate,
)
from server import (
    DEFAULT_DOWNLINK_PHRASE,
    DEFAULT_PHRASE,
    DOWNLINK_PCM_SAMPLE_RATE,
    DownlinkAudioTrack,
    keep_host_candidate,
)


class ProtocolTests(unittest.TestCase):
    def test_envelope_is_compact_and_versioned(self) -> None:
        self.assertEqual(
            envelope("welcome", 2),
            '{"v":1,"type":"welcome","session_id":"mac-lab","seq":2}',
        )

    def test_word_error_rate(self) -> None:
        self.assertEqual(word_error_rate("one two", "one two"), 0.0)
        self.assertEqual(word_error_rate("one two", "one"), 0.5)

    def test_capture_correlation_preserves_decimal_u64_strings(self) -> None:
        self.assertEqual(
            capture_correlation({
                "capture_id": "18446744073709551615",
                "request_id": "0",
            }),
            {
                "capture_id": "18446744073709551615",
                "request_id": "0",
            },
        )

    def test_capture_correlation_rejects_missing_noncanonical_or_overflow(self) -> None:
        invalid = (
            {},
            {"capture_id": 1, "request_id": "1"},
            {"capture_id": "0", "request_id": "1"},
            {"capture_id": "01", "request_id": "1"},
            {"capture_id": "1", "request_id": "-1"},
            {"capture_id": "18446744073709551616", "request_id": "1"},
        )
        for payload in invalid:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                capture_correlation(payload)

    def test_echo_bridge_targets_current_guest_and_echoes_correlation(self) -> None:
        self.assertEqual(
            current_guest_capture_request(8_000),
            {"duration_ms": 8_000, "target": "current_guest"},
        )
        self.assertEqual(
            correlated_transcript(
                "hello",
                {"capture_id": "19", "request_id": "19"},
            ),
            {"text": "hello", "capture_id": "19", "request_id": "19"},
        )

    def test_host_candidate_filter(self) -> None:
        sdp = (
            "v=0\r\n"
            "a=candidate:1 1 UDP 1 192.168.1.95 5000 typ host\r\n"
            "a=candidate:3 1 TCP 1 192.168.1.95 5002 typ host\r\n"
            "a=candidate:2 1 UDP 1 2601:db8::1 5001 typ host\r\n"
        )
        filtered = keep_host_candidate(sdp, "192.168.1.95")
        self.assertEqual(filtered.count("192.168.1.95"), 1)
        self.assertNotIn("2601:db8", filtered)

    def test_packet_timing_gates_duration_and_bursts(self) -> None:
        healthy = packet_timing_metrics([1.0, 1.02, 1.04, 1.06], 80.0)
        self.assertTrue(healthy["passed"])
        burst = packet_timing_metrics([1.0, 1.005, 1.025, 1.045], 80.0)
        self.assertFalse(burst["gates"]["packet_interval"])

    def test_capture_analysis_uses_opening_and_closing_markers(self) -> None:
        sample_rate = 8_000
        samples = array.array("h", [0]) * (sample_rate // 2)
        samples.extend(make_tone_pcm(660, 900, sample_rate=sample_rate))
        samples.extend(array.array("h", [0]) * (sample_rate * 300 // 1_000))
        samples.extend(make_tone_pcm(220, 1_000, sample_rate=sample_rate))
        samples.extend(array.array("h", [0]) * (sample_rate * 200 // 1_000))
        samples.extend(make_tone_pcm(880, 300, sample_rate=sample_rate))
        # This unrelated tail changes the informative active-RMS span but must
        # not change marker-based program duration.
        samples.extend(array.array("h", [200]) * (sample_rate // 2))
        analysis = analyze_capture_samples(
            samples,
            sample_rate,
            expected_tone_hz=660,
            expected_tone_duration_ms=900,
            expected_closing_tone_hz=880,
            expected_closing_tone_duration_ms=300,
            expected_marker_offset_ms=2_400,
            expected_program_duration_ms=2_700,
        )
        self.assertTrue(analysis["passed"], analysis)
        self.assertAlmostEqual(analysis["tone"]["frequency_hz"], 660, delta=1)
        self.assertAlmostEqual(
            analysis["closing_tone"]["frequency_hz"], 880, delta=1
        )
        self.assertAlmostEqual(
            analysis["marker_offset"]["observed_ms"], 2_400, delta=20
        )

    def test_downlink_conformance_phrase_is_separate(self) -> None:
        self.assertNotEqual(DEFAULT_DOWNLINK_PHRASE, DEFAULT_PHRASE)
        self.assertEqual(
            DEFAULT_DOWNLINK_PHRASE.lower().count("please set the timer"), 2
        )

    def test_capture_analysis_trims_quiet_tone_reverberation(self) -> None:
        sample_rate = 8_000
        samples = array.array("h", [0]) * (sample_rate // 2)
        samples.extend(make_tone_pcm(660, 900, sample_rate=sample_rate))
        samples.extend(make_tone_pcm(
            660, 240, amplitude=3_000, sample_rate=sample_rate
        ))
        samples.extend(array.array("h", [0]) * (sample_rate * 260 // 1_000))
        samples.extend(make_tone_pcm(220, 1_000, sample_rate=sample_rate))
        samples.extend(array.array("h", [0]) * (sample_rate * 200 // 1_000))
        samples.extend(make_tone_pcm(880, 300, sample_rate=sample_rate))
        analysis = analyze_capture_samples(
            samples,
            sample_rate,
            expected_tone_hz=660,
            expected_tone_duration_ms=900,
            expected_closing_tone_hz=880,
            expected_closing_tone_duration_ms=300,
            expected_marker_offset_ms=2_400,
            expected_program_duration_ms=2_700,
        )
        self.assertTrue(analysis["gates"]["tone_duration"], analysis)
        self.assertAlmostEqual(
            analysis["tone"]["duration_ms"], 900, delta=90
        )

    def test_capture_analysis_requires_closing_marker(self) -> None:
        sample_rate = 8_000
        samples = array.array("h", [0]) * (sample_rate // 2)
        samples.extend(make_tone_pcm(660, 900, sample_rate=sample_rate))
        samples.extend(array.array("h", [0]) * (sample_rate * 300 // 1_000))
        samples.extend(make_tone_pcm(220, 1_000, sample_rate=sample_rate))
        analysis = analyze_capture_samples(
            samples,
            sample_rate,
            expected_tone_hz=660,
            expected_tone_duration_ms=900,
            expected_closing_tone_hz=880,
            expected_closing_tone_duration_ms=300,
            expected_marker_offset_ms=2_400,
            expected_program_duration_ms=2_700,
        )
        self.assertFalse(analysis["closing_tone"]["detected"])
        self.assertFalse(analysis["gates"]["closing_tone_frequency"])
        self.assertFalse(analysis["gates"]["program_duration"])

    def test_firmware_playback_telemetry(self) -> None:
        line = (
            "I voice: downlink playback stopped received=180 queued=180 "
            "submitted=180 rejected=0 dropped=0 underflow=0 prebuffer=1 "
            "high_water=4 speaker_fail=0 peak=12000 near_full=0 volume=96 "
            "codec=3 pcm_rate=16000 slot=2"
        )
        telemetry = parse_playback_telemetry(line)
        self.assertIsNotNone(telemetry)
        self.assertEqual(telemetry["received"], 180)
        self.assertEqual(telemetry["speaker_fail"], 0)
        self.assertEqual(telemetry["codec"], 3)
        self.assertEqual(telemetry["pcm_rate"], 16_000)

    def test_inter_run_gap_schedule(self) -> None:
        self.assertEqual(expand_inter_run_gaps("0,2,10", 3), [0.0, 2.0, 10.0])
        self.assertEqual(expand_inter_run_gaps("2", 3), [2.0, 2.0, 2.0])
        with self.assertRaises(ValueError):
            expand_inter_run_gaps("0,2", 3)


class DownlinkTrackTests(unittest.IsolatedAsyncioTestCase):
    async def test_track_uses_monotonic_pts_and_never_catches_up(self) -> None:
        track = DownlinkAudioTrack()
        try:
            first, end = track.enqueue_program(make_tone_pcm(
                660, 80, sample_rate=DOWNLINK_PCM_SAMPLE_RATE
            ))
            frames = [await track.recv() for _ in range(end - first)]
            self.assertEqual([frame.pts for frame in frames], [0, 320, 640, 960])
            self.assertEqual(
                [frame.sample_rate for frame in frames], [16_000] * 4
            )
            metrics = packet_timing_metrics(
                track.packet_times[first:end],
                expected_duration_ms=80,
            )
            self.assertTrue(metrics["passed"], metrics)
            self.assertGreaterEqual(metrics["min_packet_interval_ms"], 10.0)
        finally:
            track.stop()


if __name__ == "__main__":
    unittest.main()
