from __future__ import annotations

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

from doodad_agent.attention import AttentionBroker
from doodad_agent.builder import CompositeBuilder
from doodad_agent.capabilities import CapabilityKernel
from doodad_agent.controller import ForegroundController
from doodad_agent.fake_worker import FakeAppBuilder, ManualClock
from doodad_agent.fake_work_worker import FakeWorkBuilder
from doodad_agent.jobs import JobManager
from doodad_agent.storage import Store


ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.doodad_cli.contract import build_and_stage  # noqa: E402
from tools.doodad_cli.native import NativeHost  # noqa: E402


def test_microphone_free_parallel_agent_end_to_end(tmp_path: Path) -> None:
    """Exercise text request -> durable jobs -> wire projection -> watch UI."""

    clock = ManualClock(10_000)
    store = Store(tmp_path / "parallel-e2e.db")
    jobs = JobManager(store, "t-watch-e2e")
    builder = CompositeBuilder(FakeAppBuilder(jobs), FakeWorkBuilder(jobs))
    attention = AttentionBroker(store, jobs)
    controller = ForegroundController(
        CapabilityKernel(store, clock.now_ms, jobs.device_id),
        builder,
        attention,
        now_ms=lambda: clock.now_ms,
    )
    staged = build_and_stage(ROOT, ROOT / "apps" / "timer")
    try:
        controller.fake_reply(
            "Build me a hydration tracker app", "e2e-app"
        )
        clock.advance(1)
        controller.fake_reply(
            "Research local-first agent interfaces and create a report",
            "e2e-research",
        )
        clock.advance(1)
        controller.fake_reply(
            "Create a six-slide deck and email it to pat@example.com",
            "e2e-slides",
        )

        # Foreground conversation remains independent while all three jobs run.
        assert "Squat" in controller.fake_reply(
            "What is my next set?", "e2e-unrelated"
        )
        assert "Research Report" in controller.fake_reply(
            "How is the research report going?", "e2e-status"
        )

        background = attention.background_snapshot(clock.now_ms)
        tasks = background["tasks"]
        assert background["running_count"] == len(tasks) == 3
        state_document = {
            "schema_version": 1,
            "device_id": jobs.device_id,
            "voice_phase": "ready",
            "display": {"transcript": "", "response": ""},
            "background": {
                "running_count": background["running_count"],
                "focused_question": False,
                "review_ready": False,
                "completion_pending": False,
                "status_changed": background["status_changed"],
                "install_state": 0,
                "tasks": tasks,
            },
        }
        schema = json.loads(
            (ROOT / "contracts/agent-state-v1.schema.json").read_text(
                encoding="utf-8"
            )
        )
        Draft202012Validator.check_schema(schema)
        Draft202012Validator(schema).validate(state_document)
        assert len(json.dumps(state_document).encode("utf-8")) < 16_384

        with NativeHost(ROOT) as host:
            host.start_system_shell(
                app_id="dev.doodad.timer",
                app_name="Timer",
                app_detail="Version 1.0.0  •  ready",
                wasm_path=staged.wasm,
            )
            default_home = host.framebuffer_rgb565()
            host.set_agent_tasks(
                tasks,
                active_count=background["running_count"],
                status_changed=False,
            )
            live_home = host.framebuffer_rgb565()
            assert live_home != default_home

            host.click_system_action("system.agents")
            agents_before = host.framebuffer_rgb565()
            details: list[bytes] = []
            for task in tasks:
                host.click_system_action(str(task["job_id"]))
                assert host.system_surface() == "agent_detail"
                details.append(host.framebuffer_rgb565())
                host.click_system_action("system.agent.back")
                assert host.system_surface() == "agents"
                assert host.framebuffer_rgb565() == agents_before
            assert len(set(details)) == 3

            # A real ledger update keeps an open detail bound to its durable ID,
            # even when recency reorders the list.
            research_task = next(
                task for task in tasks if task["kind"] == "research_report"
            )
            host.click_system_action(str(research_task["job_id"]))
            detail_before_progress = host.framebuffer_rgb565()
            clock.advance(5_000)
            builder.tick(clock.now_ms)
            progressed = attention.background_snapshot(clock.now_ms)
            host.set_agent_tasks(
                progressed["tasks"],
                active_count=progressed["running_count"],
                status_changed=True,
            )
            assert host.system_surface() == "agent_detail"
            assert host.framebuffer_rgb565() != detail_before_progress
            host.click_system_action("system.agent.back")
            assert host.system_surface() == "agents"
            assert host.framebuffer_rgb565() != agents_before
            assert "drafting" in controller.fake_reply(
                "What's the progress on the research report?", "e2e-progress"
            )

            # Status talk does not consume or block an unrelated new turn.
            assert "Squat" in controller.fake_reply(
                "What is my next set?", "e2e-after-status"
            )
    finally:
        builder.close()
        store.close()
