from __future__ import annotations

import json
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker


ROOT = Path(__file__).resolve().parents[3]


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate(schema_name: str, document: dict) -> None:
    schema = load(ROOT / "contracts" / schema_name)
    Draft202012Validator.check_schema(schema)
    Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)


def test_agent_contracts_validate() -> None:
    for app in ("workout", "calories", "timer"):
        validate("agent-contract-v1.schema.json", load(ROOT / "apps" / app / "agent.json"))


def test_watch_and_job_fixtures_validate() -> None:
    validate(
        "watch-state-v1.schema.json",
        load(ROOT / "fixtures" / "agent" / "watch-state" / "workout-active.json"),
    )
    validate(
        "job-question-v1.schema.json",
        load(ROOT / "fixtures" / "agent" / "jobs" / "layout-question.json"),
    )
    validate(
        "job-event-v1.schema.json",
        load(ROOT / "fixtures" / "agent" / "jobs" / "completed-event.json"),
    )
