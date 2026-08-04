"""Common non-blocking interface for fake and Codex app builders."""

from __future__ import annotations

from typing import Protocol


class AppBuilder(Protocol):
    def start(self, brief: str, now_ms: int) -> str: ...

    def tick(self, now_ms: int) -> list[str]: ...

    def close(self) -> None: ...
