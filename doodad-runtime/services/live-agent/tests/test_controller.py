from pathlib import Path

from doodad_agent.attention import AttentionBroker
from doodad_agent.capabilities import CapabilityKernel
from doodad_agent.controller import ForegroundController, normalize_choice
from doodad_agent.fake_worker import FakeAppBuilder, ManualClock
from doodad_agent.jobs import JobManager
from doodad_agent.storage import Store


def stack(path: Path):
    clock = ManualClock(1_000)
    store = Store(path)
    jobs = JobManager(store)
    kernel = CapabilityKernel(store, clock.now_ms)
    builder = FakeAppBuilder(jobs)
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
