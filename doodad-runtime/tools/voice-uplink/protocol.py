from __future__ import annotations

import json
import re
from typing import Any


UINT64_MAX = (1 << 64) - 1
DECIMAL_UINT64 = re.compile(r"(?:0|[1-9][0-9]{0,19})\Z")


def envelope(message_type: str, sequence: int, payload: dict[str, Any] | None = None) -> str:
    message: dict[str, Any] = {
        "v": 1,
        "type": message_type,
        "session_id": "mac-lab",
        "seq": sequence,
    }
    if payload is not None:
        message["payload"] = payload
    return json.dumps(message, separators=(",", ":"))


def capture_correlation(payload: dict[str, Any]) -> dict[str, str]:
    """Return the canonical firmware-issued correlation for transcript echo."""
    correlation: dict[str, str] = {}
    for name, allow_zero in (("capture_id", False), ("request_id", True)):
        value = payload.get(name)
        if not isinstance(value, str) or DECIMAL_UINT64.fullmatch(value) is None:
            raise ValueError(f"missing or invalid {name}")
        parsed = int(value)
        if parsed > UINT64_MAX or (not allow_zero and parsed == 0):
            raise ValueError(f"missing or invalid {name}")
        correlation[name] = value
    return correlation


def current_guest_capture_request(duration_ms: int) -> dict[str, Any]:
    if (
        not isinstance(duration_ms, int)
        or isinstance(duration_ms, bool)
        or not 1_000 <= duration_ms <= 30_000
    ):
        raise ValueError("duration_ms must be an integer in 1000..30000")
    return {"duration_ms": duration_ms, "target": "current_guest"}


def correlated_transcript(
    text: str,
    capture_status: dict[str, Any],
) -> dict[str, str]:
    if not isinstance(text, str) or not text:
        raise ValueError("transcript text must be non-empty")
    return {"text": text, **capture_correlation(capture_status)}


def normalize_words(text: str) -> list[str]:
    return re.findall(r"[a-z0-9]+", text.lower())


def word_error_rate(reference: str, hypothesis: str) -> float:
    left = normalize_words(reference)
    right = normalize_words(hypothesis)
    if not left:
        return 0.0 if not right else 1.0
    row = list(range(len(right) + 1))
    for index, expected in enumerate(left, 1):
        next_row = [index]
        for column, actual in enumerate(right, 1):
            next_row.append(min(
                next_row[-1] + 1,
                row[column] + 1,
                row[column - 1] + (expected != actual),
            ))
        row = next_row
    return row[-1] / len(left)
