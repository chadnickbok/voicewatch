from pathlib import Path

from doodad_agent.attention import AttentionBroker
from doodad_agent.builder import CompositeBuilder
from doodad_agent.capabilities import CapabilityKernel
from doodad_agent.controller import ForegroundController, normalize_choice
from doodad_agent.fake_worker import FakeAppBuilder, ManualClock
from doodad_agent.fake_work_worker import FakeWorkBuilder
from doodad_agent.jobs import JobManager
from doodad_agent.storage import Store


def stack(path: Path):
    clock = ManualClock(1_000)
    store = Store(path)
    jobs = JobManager(store)
    kernel = CapabilityKernel(store, clock.now_ms)
    builder = CompositeBuilder(FakeAppBuilder(jobs), FakeWorkBuilder(jobs))
    attention = AttentionBroker(store, jobs)
    controller = ForegroundController(
        kernel, builder, attention, now_ms=lambda: clock.now_ms
    )
    return clock, store, jobs, builder, attention, controller


def observe_all(jobs: JobManager, attention: AttentionBroker, job_id: str, now_ms: int):
    for event in jobs.events(job_id):
        attention.observe(event, now_ms)


def test_foreground_continues_and_focus_routes_without_cross_talk(tmp_path):
    clock, store, jobs, builder, attention, controller = stack(tmp_path / "control.db")
    try:
        first = controller.start_app_build("rest timer one")["job_id"]
        second = controller.start_app_build("rest timer two")["job_id"]
        reply = controller.fake_reply("What is my next set?", "foreground-1")
        assert "Squat" in reply
        assert jobs.job(first)["state"] == "running"
        assert jobs.job(second)["state"] == "running"

        clock.advance(10_000)
        builder.tick(clock.now_ms)
        observe_all(jobs, attention, first, clock.now_ms)
        observe_all(jobs, attention, second, clock.now_ms)
        question = attention.natural_pause(clock.now_ms)
        assert question is not None and question.job_id in {first, second}
        focused_job = question.job_id
        other_job = second if focused_job == first else first
        assert controller.fake_reply("the ring", "answer-1").startswith("Got it")
        assert [event.kind for event in jobs.events(focused_job)][-1] == "input_received"
        assert [event.kind for event in jobs.events(other_job)][-1] == "needs_input"
    finally:
        store.close()


def test_focused_answer_is_typed_and_duplicate_safe(tmp_path):
    clock, store, jobs, builder, attention, controller = stack(tmp_path / "control.db")
    try:
        job_id = controller.start_app_build("rest timer")["job_id"]
        clock.advance(10_000)
        builder.tick(clock.now_ms)
        observe_all(jobs, attention, job_id, clock.now_ms)
        attention.natural_pause(clock.now_ms)
        assert not controller.route_focused("maybe the ring or bar", "ambiguous").handled
        routed = controller.route_focused("the horizontal bar", "answer")
        assert routed.handled and routed.answer == "bar"
        assert not controller.route_focused("bar", "answer").handled
    finally:
        store.close()


def test_choice_normalization_requires_one_enum_value():
    assert normalize_choice("The ring, please.", ["ring", "bar"]) == "ring"
    assert normalize_choice(
        "Use the horizontal bar please", ["circular ring", "horizontal bar"]
    ) == "horizontal bar"
    assert normalize_choice("ring or bar", ["ring", "bar"]) is None
    assert normalize_choice("surprise me", ["ring", "bar"]) is None


def test_focused_open_string_question_captures_bounded_voice_feedback(tmp_path):
    clock, store, jobs, _builder, attention, controller = stack(
        tmp_path / "open-answer.db"
    )
    try:
        job_id = jobs.create("codex_app_build", {"brief": "timer"}, clock.now_ms)
        jobs.append(
            job_id,
            "needs_input",
            "Plan ready.",
            {
                "question": {
                    "id": "plan-approval-test",
                    "prompt": "Say approve or describe a revision.",
                    "answer_schema": {"type": "string", "maxLength": 320},
                }
            },
            "test",
            clock.now_ms,
        )
        jobs.focus(job_id, "plan-approval-test")
        routed = controller.route_focused(
            "Make the primary action calmer and move it below the timer.",
            "voice-revision",
        )
        assert routed.handled
        assert routed.answer == (
            "Make the primary action calmer and move it below the timer."
        )
    finally:
        store.close()


def test_three_parallel_task_types_remain_queryable_during_new_conversation(tmp_path):
    clock, store, jobs, builder, attention, controller = stack(tmp_path / "parallel.db")
    try:
        app_reply = controller.fake_reply(
            "Build me a hydration tracker app", "parallel-app"
        )
        clock.advance(1)
        research_reply = controller.fake_reply(
            "Research local-first agent interfaces and create a report",
            "parallel-research",
        )
        clock.advance(1)
        deck_reply = controller.fake_reply(
            "Create a slide deck and email it to pat@example.com",
            "parallel-deck",
        )
        assert all("background" in reply for reply in (
            app_reply, research_reply, deck_reply
        ))
        assert "Squat" in controller.fake_reply(
            "What is my next set?", "parallel-conversation"
        )

        snapshot = attention.background_snapshot(clock.now_ms)
        assert snapshot["running_count"] == 3
        assert [task["kind"] for task in snapshot["tasks"]] == [
            "presentation_delivery", "research_report", "app_build"
        ]
        assert [task["title"] for task in snapshot["tasks"]] == [
            "SLIDE DECK", "RESEARCH REPORT", "BUILDING APP"
        ]

        status = controller.fake_reply(
            "What is the status of the research report?", "parallel-status"
        )
        assert "Research Report" in status
        assert "researching" in status
        all_status = controller.fake_reply(
            "What are the agents doing?", "parallel-all-status"
        )
        assert all(name in all_status for name in (
            "Building App", "Research Report", "Slide Deck"
        ))
        assert len(controller.task_status()["tasks"]) == 3

        clock.advance(5_000)
        builder.tick(clock.now_ms)
        report = controller.task_status("research report")["tasks"]
        assert len(report) == 1
        assert report[0]["progress"] == 55
        assert report[0]["status"] == "DRAFTING"
        assert attention.background_snapshot(clock.now_ms)["status_changed"]
        clock.advance(30_001)
        assert not attention.background_snapshot(clock.now_ms)["status_changed"]
    finally:
        builder.close()
        store.close()
