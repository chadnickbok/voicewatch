from __future__ import annotations

import json
import re
from typing import Any


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
