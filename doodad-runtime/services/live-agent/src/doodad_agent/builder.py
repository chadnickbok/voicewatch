"""Common non-blocking interface for fake and Codex app builders."""

from __future__ import annotations

from typing import Protocol


class AppBuilder(Protocol):
    def start(self, brief: str, now_ms: int) -> str: ...

    def tick(self, now_ms: int) -> list[str]: ...

    def close(self) -> None: ...


class WorkBuilder(Protocol):
    def start_work(
        self,
        kind: str,
        brief: str,
        now_ms: int,
        *,
        recipient: str | None = None,
    ) -> str: ...

    def tick(self, now_ms: int) -> list[str]: ...

    def close(self) -> None: ...


class CompositeBuilder:
    """Present app and general workers as one lifecycle to the conversation."""

    def __init__(self, apps: AppBuilder, work: WorkBuilder) -> None:
        self.apps = apps
        self.work = work

    def start(self, brief: str, now_ms: int) -> str:
        return self.apps.start(brief, now_ms)

    def start_work(
        self,
        kind: str,
        brief: str,
        now_ms: int,
        *,
        recipient: str | None = None,
    ) -> str:
        return self.work.start_work(
            kind, brief, now_ms, recipient=recipient
        )

    def tick(self, now_ms: int) -> list[str]:
        return [*self.apps.tick(now_ms), *self.work.tick(now_ms)]

    def close(self) -> None:
        self.apps.close()
        self.work.close()
