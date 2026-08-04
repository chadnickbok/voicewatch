"""Small JSONL latency trace used by physical and fake-system evidence lanes."""

from __future__ import annotations

import json
import time
from pathlib import Path
from threading import Lock
from typing import Any


class LatencyTrace:
    """Append monotonic events without ever recording provider credentials."""

    def __init__(self, path: Path | None = None) -> None:
        self.path = path
        self._origin_ns = time.monotonic_ns()
        self._lock = Lock()
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)

    def mark(self, kind: str, **fields: Any) -> dict[str, Any]:
        event = {
            "kind": kind,
            "monotonic_ms": round((time.monotonic_ns() - self._origin_ns) / 1_000_000, 3),
            **fields,
        }
        if self.path is not None:
            line = json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n"
            with self._lock, self.path.open("a", encoding="utf-8") as stream:
                stream.write(line)
        return event


class DeviceLatencyTrace:
    """Attach a stable device identity to every child-runtime event."""

    def __init__(self, parent: LatencyTrace, device_id: str) -> None:
        self.parent = parent
        self.device_id = device_id

    def mark(self, kind: str, **fields: Any) -> dict[str, Any]:
        return self.parent.mark(kind, device_id=self.device_id, **fields)
