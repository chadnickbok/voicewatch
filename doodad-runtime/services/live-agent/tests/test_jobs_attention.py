from __future__ import annotations

import pytest

from doodad_agent.attention import AttentionBroker
from doodad_agent.fake_worker import FakeAppBuilder, ManualClock
from doodad_agent.jobs import JobManager
from doodad_agent.storage import Store


def observe_new(broker, jobs, job_id, seen, now_ms):
    for event in jobs.events(job_id):
        if event.event_id not in seen:
            broker.observe(event, now_ms)
            seen.add(event.event_id)


def test_fake_worker_focus_restart_and_one_time_completion(tmp_path) -> None:
    path = tmp_path / "agent.sqlite3"
    clock = ManualClock(1_000)
    store = Store(path)
    jobs = JobManager(store)
    worker = FakeAppBuilder(jobs)
    broker = AttentionBroker(store, jobs)
    job_id = worker.start("Build a rest timer", clock.now_ms)
    seen = set()
    observe_new(broker, jobs, job_id, seen, clock.now_ms)
    assert jobs.job(job_id)["state"] == "running"
    assert broker.natural_pause(clock.now_ms) is None

    worker.tick(clock.advance(10_000))
    observe_new(broker, jobs, job_id, seen, clock.now_ms)
    question = broker.natural_pause(clock.now_ms)
    assert question.kind == "question"
    assert question.question_id == "layout"
    assert broker.background_snapshot()["running_count"] == 1
    assert broker.natural_pause(clock.now_ms) is None
    with pytest.raises(ValueError):
        broker.answer_focused("banana", "utt-unrelated", clock.now_ms)
    assert broker.answer_focused("ring", "utt-layout", clock.now_ms)
    assert not broker.answer_focused("ring", "utt-layout", clock.now_ms)

    store.close()
    store = Store(path)
    jobs = JobManager(store)
    worker = FakeAppBuilder(jobs)
    broker = AttentionBroker(store, jobs)
    assert jobs.job(job_id)["state"] == "running"
    worker.tick(clock.advance(30_000))
    observe_new(broker, jobs, job_id, seen, clock.now_ms)
    assert jobs.rebuild_state(job_id) == jobs.job(job_id)["state"] == "completed"
    completion = broker.natural_pause(clock.now_ms)
    assert completion.kind == "announcement"
    assert completion.text == "Your rest-timer design is ready."
    assert broker.natural_pause(clock.now_ms) is None


def test_progress_is_visual_only_and_two_jobs_do_not_cross_route(tmp_path) -> None:
    clock = ManualClock()
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store)
    worker = FakeAppBuilder(jobs)
    broker = AttentionBroker(store, jobs)
    first = worker.start("First", clock.now_ms)
    second = worker.start("Second", clock.now_ms + 1)
    worker.tick(clock.advance(10_001))
    seen = set()
    for job_id in (first, second):
        observe_new(broker, jobs, job_id, seen, clock.now_ms)
    focused = broker.natural_pause(clock.now_ms)
    assert focused.job_id == first
    assert broker.answer_focused("bar", "utt-first-layout", clock.now_ms)
    next_question = broker.natural_pause(clock.now_ms)
    assert next_question.job_id == second
    assert jobs.open_questions()[0]["job_id"] == second

    progress = jobs.append(
        first, "progress", "Halfway there.", {"progress": 50}, "test", clock.now_ms
    )
    broker.observe(progress, clock.now_ms)
    assert broker.natural_pause(clock.now_ms) is None


def test_expired_lease_recovers_to_queue(tmp_path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store)
    job_id = jobs.create("fake_app_build", {}, 0)
    jobs.append(job_id, "started", "started", {}, "test", 1)
    with store.transaction() as connection:
        connection.execute(
            "UPDATE jobs SET lease_owner='worker',lease_expires_ms=5 WHERE job_id=?", (job_id,)
        )
        connection.execute(
            "INSERT INTO worker_leases VALUES(?,?,?,?,?)", (job_id, "worker", 5, 1, 1)
        )
    assert jobs.recover_expired(6) == [job_id]
    assert jobs.job(job_id)["state"] == "queued"
