import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from protocol import envelope, word_error_rate
from server import keep_host_candidate


class ProtocolTests(unittest.TestCase):
    def test_envelope_is_compact_and_versioned(self) -> None:
        self.assertEqual(
            envelope("welcome", 2),
            '{"v":1,"type":"welcome","session_id":"mac-lab","seq":2}',
        )

    def test_word_error_rate(self) -> None:
        self.assertEqual(word_error_rate("one two", "one two"), 0.0)
        self.assertEqual(word_error_rate("one two", "one"), 0.5)

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


if __name__ == "__main__":
    unittest.main()
