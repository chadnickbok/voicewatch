"""Provider-independent foreground decisions and focused-answer routing."""

from __future__ import annotations

import re
import time
import uuid
from dataclasses import dataclass
from typing import Any

from .attention import AttentionBroker
from .builder import AppBuilder
from .capabilities import CapabilityKernel


def normalize_choice(text: str, choices: list[object]) -> object | None:
    """Resolve one whole enum label, including multiword voice choices."""
    normalized = " ".join(re.findall(r"[a-z0-9]+", text.casefold()))
    padded = f" {normalized} "
    matches = []
    for choice in choices:
        candidate = " ".join(re.findall(r"[a-z0-9]+", str(choice).casefold()))
        if candidate and f" {candidate} " in padded:
            matches.append(choice)
    return matches[0] if len(matches) == 1 else None


@dataclass(frozen=True)
class RoutedAnswer:
    handled: bool
    answer: object | None = None
    job_id: str | None = None
    question_id: str | None = None


class ForegroundController:
    """Deterministic layer beneath both hosted and fake conversation models."""

    def __init__(
        self,
        kernel: CapabilityKernel,
        builder: AppBuilder,
        attention: AttentionBroker,
        now_ms: callable | None = None,
    ) -> None:
        self.kernel = kernel
        self.builder = builder
        self.attention = attention
        self._now_ms = now_ms or (lambda: int(time.time() * 1000))

    def route_focused(self, text: str, utterance_id: str) -> RoutedAnswer:
        focused = self.attention.jobs.focused()
        if focused is None:
            return RoutedAnswer(False)
        answer_schema = focused["answer_schema"]
        choices = answer_schema.get("enum")
        if isinstance(choices, list):
            answer = normalize_choice(text, choices)
        elif answer_schema.get("type") == "string":
            answer = " ".join(text.split())[:320] or None
        else:
            answer = None
        if answer is None:
            return RoutedAnswer(False)
        handled = self.attention.answer_focused(answer, utterance_id, self._now_ms())
        return RoutedAnswer(
            handled, answer, focused["job_id"], focused["question_id"]
        )

    def record_missed_set(self, idempotency_key: str) -> dict[str, Any]:
        state = self.kernel.snapshot()
        return self.kernel.record_missed_set(
            workout_id=str(state["active_workout_id"]),
            set_id=str(state["selected_entity"]),
            expected_revision=int(state["revision"]),
            idempotency_key=idempotency_key,
            now_ms=self._now_ms(),
        )

    def get_next_set(self) -> dict[str, Any] | None:
        return self.kernel.get_next_set()

    def log_food(
        self,
        description: str,
        quantity: float = 1,
        unit: str = "item",
        idempotency_key: str | None = None,
    ) -> dict[str, Any]:
        return self.kernel.log_food(
            description=description,
            quantity=quantity,
            unit=unit,
            idempotency_key=idempotency_key or f"utterance_{uuid.uuid4().hex}",
            now_ms=self._now_ms(),
        )

    def start_app_build(self, brief: str) -> dict[str, Any]:
        job_id = self.builder.start(brief, self._now_ms())
        return {
            "accepted": True,
            "job_id": job_id,
            "state": self.attention.jobs.job(job_id)["state"],
        }

    def start_background_work(
        self,
        kind: str,
        brief: str,
        *,
        recipient: str | None = None,
    ) -> dict[str, Any]:
        start_work = getattr(self.builder, "start_work", None)
        if start_work is None:
            raise RuntimeError("general background work is not configured")
        job_id = start_work(
            kind,
            brief,
            self._now_ms(),
            recipient=recipient,
        )
        return {
            "accepted": True,
            "job_id": job_id,
            "kind": kind,
            "state": self.attention.jobs.job(job_id)["state"],
        }

    def task_status(self, query: str | None = None) -> dict[str, Any]:
        tasks = self.attention.jobs.task_status(self._now_ms(), query)
        return {
            "count": len(tasks),
            "tasks": tasks,
        }

    def fake_reply(self, utterance: str, utterance_id: str | None = None) -> str:
        """Offline CI lane proving foreground work continues during jobs."""
        routed = self.route_focused(utterance, utterance_id or f"utt_{uuid.uuid4().hex}")
        if routed.handled:
            return f"Got it — {routed.answer}. The build is continuing."
        text = utterance.casefold()
        if "missed" in text and "set" in text:
            result = self.record_missed_set(utterance_id or f"utt_{uuid.uuid4().hex}")
            return f"Recorded {result['set_id']} as missed."
        if "next" in text and "set" in text:
            result = self.get_next_set()
            return "There is no next pending set." if result is None else (
                f"Next is {result['exercise']}: {result['weight_lb']} pounds for {result['reps']}."
            )
        words = set(re.findall(r"[a-z0-9]+", text))
        if words.intersection({"ate", "food", "bagel"}):
            self.log_food(utterance, idempotency_key=utterance_id)
            return "Logged that provisionally."
        if any(word in text for word in ("status", "progress", "doing", "going")):
            result = self.task_status(utterance)
            tasks = result["tasks"]
            if not tasks:
                return "I don't have a matching background task."
            descriptions = [
                f"{task['title'].title()} is {task['status'].lower()} at {task['progress']} percent"
                for task in tasks[:3]
            ]
            return "; ".join(descriptions) + "."
        if "research" in text or ("report" in text and "create" in text):
            result = self.start_background_work("research_report", utterance)
            return f"Started the research report in the background as {result['job_id']}."
        if any(term in text for term in ("slide deck", "slides", "presentation")):
            match = re.search(r"[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}", utterance, re.I)
            result = self.start_background_work(
                "presentation_delivery",
                utterance,
                recipient=match.group(0) if match else None,
            )
            return f"Started the slide deck workflow in the background as {result['job_id']}."
        if "build" in text or ("make" in text and "app" in text):
            result = self.start_app_build(utterance)
            return f"Started it in the background as {result['job_id']}."
        return "I’m listening; the background work is still independent."
