from __future__ import annotations

import json
import threading
import time
from pathlib import Path

from doodad_agent.app_verifier import VerifiedArtifact, package_tree_snapshot
from doodad_agent.attention import AttentionBroker
from doodad_agent.capabilities import CapabilityKernel
from doodad_agent.codex_protocol import CodexTurnResult
from doodad_agent.codex_worker import CodexAppBuilder
from doodad_agent.controller import ForegroundController
from doodad_agent.jobs import JobManager
from doodad_agent.personal_bundle import (
    ArtifactStore,
    PersonalBundlePackager,
    PersonalTrustProfile,
    decode_personal_bundle,
)
from doodad_agent.storage import Store


RUNTIME_ROOT = Path(__file__).resolve().parents[3]


def wait_for(predicate, timeout: float = 5) -> None:  # type: ignore[no-untyped-def]
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError("condition did not become true")


class ScriptedClient:
    def __init__(self, result: str, calls: list[dict[str, object]]) -> None:
        self.result = result
        self.calls = calls

    def run_turn(self, **arguments):  # type: ignore[no-untyped-def]
        thread_id = arguments["thread_id"] or "thread-rest-timer"
        turn_id = f"turn-{len(self.calls) + 1}"
        arguments["on_started"](thread_id, turn_id)
        self.calls.append(
            {
                "thread_id": arguments["thread_id"],
                "prompt": arguments["prompt"],
                "workspace": arguments["workspace"],
            }
        )
        result = self.result
        if result == "needs_input" and "Plan and implement" in str(arguments["prompt"]):
            answer = arguments["on_question"](
                {
                    "questions": [
                        {
                            "id": "layout",
                            "header": "Layout",
                            "question": "Should progress use a ring or a horizontal bar?",
                            "options": [
                                {"label": "ring", "description": "Circular progress."},
                                {"label": "bar", "description": "Linear progress."},
                            ],
                        }
                    ]
                }
            )
            if answer is None:
                return CodexTurnResult(thread_id, turn_id, "failed", "", "stopped")
            result = "ready"
        elif result == "needs_input":
            result = "ready"
        if result == "ready":
            workspace = Path(arguments["workspace"])
            (workspace / "BUILD_PLAN.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "product_summary": "A generated rest timer.",
                        "app_id": "dev.doodad.generated-rest",
                        "name": "Lift Rest",
                        "version": "0.1.0",
                        "identity": {"icon": "timer", "theme_seed": "#20BFF4"},
                        "capabilities": ["ui.mount", "timer.schedule"],
                        "interactions": ["Start the timer"],
                        "scenarios": [
                            {
                                "id": "timer.baseline",
                                "kind": "timer",
                                "required_ops": ["action.dispatch", "assert.state", "clock.advance"],
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
        return CodexTurnResult(
            thread_id,
            turn_id,
            "completed",
            json.dumps({"status": result, "summary": f"{result} summary"}),
        )

    def close(self) -> None:
        return None


class FakeVerifier:
    def __init__(self) -> None:
        self.calls: list[tuple[Path, str]] = []

    def verify(self, workspace: Path, layout: str) -> VerifiedArtifact:
        self.calls.append((workspace, layout))
        package = workspace / "app" / "target" / "doodad" / "dev.doodad.generated-rest"
        return VerifiedArtifact(
            "dev.doodad.generated-rest@0.1.0",
            str(package),
            str(package / "preview.bmp"),
            "a" * 64,
            f"Generated Rest Timer ({layout}) passed 8 independent gates.",
            ("schema", "build", "check", "test", "wasm-inspect", "timer-conformance", "semantics", "simulator-render"),
        )


class RepairingVerifier(FakeVerifier):
    def verify(self, workspace: Path, layout: str) -> VerifiedArtifact:
        if not self.calls:
            self.calls.append((workspace, layout))
            from doodad_agent.app_verifier import VerificationError

            raise VerificationError("Wasm import order does not match manifest")
        return super().verify(workspace, layout)


class MaterializingVerifier(FakeVerifier):
    def verify(self, workspace: Path, layout: str) -> VerifiedArtifact:
        self.calls.append((workspace, layout))
        package = (
            workspace
            / "app"
            / "target"
            / "doodad"
            / "dev.doodad.generated-rest"
        )
        package.mkdir(parents=True, exist_ok=True)
        (package / "manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "id": "dev.doodad.generated-rest",
                    "name": "Lift Rest",
                    "version": "0.1.0",
                    "host_abi": 1,
                    "identity": {"icon": "timer", "theme_seed": "#20BFF4"},
                    "capabilities": ["ui.mount", "timer.schedule"],
                    "wasm": "app.wasm",
                }
            ),
            encoding="utf-8",
        )
        (package / "app.wasm").write_bytes(b"\0asm-generated")
        (package / "preview.bmp").write_bytes(b"BM")
        tree_sha256, _ = package_tree_snapshot(package)
        return VerifiedArtifact(
            "dev.doodad.generated-rest@0.1.0",
            str(package),
            str(package / "preview.bmp"),
            tree_sha256,
            f"Lift Rest ({layout}) passed independent gates.",
            ("schema", "build"),
        )


def observe_all(jobs: JobManager, broker: AttentionBroker, job_id: str) -> None:
    for event in jobs.events(job_id):
        broker.observe(event, int(time.time() * 1000))


def test_codex_job_persists_question_resumes_thread_and_becomes_review_ready(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "cores3-test")
    calls: list[dict[str, object]] = []
    results = iter(("needs_input", "ready"))
    verifier = FakeVerifier()

    def client_factory():  # type: ignore[no-untyped-def]
        return ScriptedClient(next(results), calls)

    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=client_factory,
        verifier=verifier,  # type: ignore[arg-type]
    )
    broker = AttentionBroker(store, jobs)
    controller = ForegroundController(
        CapabilityKernel(store, 0, jobs.device_id), builder, broker
    )
    try:
        job_id = controller.start_app_build("Please make a rest timer")["job_id"]
        assert "Squat" in controller.fake_reply("What is my next set?", "foreground")
        wait_for(lambda: jobs.job(job_id)["state"] == "needs_input")
        assert len(jobs.open_questions()) == 1
        observe_all(jobs, broker, job_id)
        action = broker.natural_pause(int(time.time() * 1000))
        assert action is not None and action.question_id == "layout"
        assert controller.route_focused("the ring", "answer-layout").handled
        assert not controller.route_focused("ring", "answer-layout").handled

        builder.tick(int(time.time() * 1000))
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")
        assert [call["thread_id"] for call in calls] == [None]
        assert verifier.calls[0][1]["name"] == "Lift Rest"
        events = jobs.events(job_id)
        assert [event.kind for event in events].count("needs_input") == 1
        artifact = events[-1].payload["artifact"]
        assert artifact["artifact_id"] == "dev.doodad.generated-rest@0.1.0"
        assert artifact["sha256"] == "a" * 64
        session = store.fetch_one(
            "SELECT * FROM codex_sessions WHERE job_id=?", (job_id,)
        )
        assert session["thread_id"] == "thread-rest-timer"
        assert json.loads(session["artifact_json"])["sha256"] == "a" * 64
        assert broker.background_snapshot()["review_ready"]
        builder.tick(int(time.time() * 1000))
        assert len(verifier.calls) == 1
    finally:
        builder.close()
        store.close()


def test_complete_brief_builds_without_interrupting_for_input(tmp_path: Path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "t-watch-no-question")
    calls: list[dict[str, object]] = []
    verifier = FakeVerifier()
    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient("ready", calls),
        verifier=verifier,  # type: ignore[arg-type]
    )
    try:
        job_id = builder.start(
            "Build a blue hydration tracker with a water-drop icon", 1
        )
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")
        assert jobs.open_questions() == []
        assert len(calls) == 1
        assert len(verifier.calls) == 1
    finally:
        builder.close()
        store.close()


def test_hello_world_brief_has_a_deterministic_no_question_product_shape(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "t-watch-hello-world")
    calls: list[dict[str, object]] = []
    verifier = FakeVerifier()
    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient("ready", calls),
        verifier=verifier,  # type: ignore[arg-type]
    )
    try:
        job_id = builder.start("Build me a hello world app", 1)
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")
        assert jobs.open_questions() == []
        workspace = Path(calls[0]["workspace"])
        brief = (workspace / "BUILD_BRIEF.md").read_text(encoding="utf-8")
        assert "Hello World" in brief
        assert "ask no follow-up question" in brief
        assert "toggles the greeting" in brief
        assert "Use only `ui.mount`" in brief
    finally:
        builder.close()
        store.close()


def test_codex_job_resumes_after_restart_while_waiting_for_layout(tmp_path: Path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "cores3-restart")
    calls: list[dict[str, object]] = []
    first = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient("needs_input", calls),
        verifier=FakeVerifier(),  # type: ignore[arg-type]
    )
    job_id = first.start("Build a timer", 1)
    wait_for(lambda: jobs.job(job_id)["state"] == "needs_input")
    first.close()

    jobs.focus(job_id, "layout")
    assert jobs.answer(job_id, "layout", "bar", "restart-answer", 2)
    verifier = FakeVerifier()
    second = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient("ready", calls),
        verifier=verifier,  # type: ignore[arg-type]
    )
    try:
        second.tick(3)
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")
        assert calls[-1]["thread_id"] == "thread-rest-timer"
        assert verifier.calls[0][1]["name"] == "Lift Rest"
    finally:
        second.close()
        store.close()


def test_independent_failure_gets_one_bounded_codex_repair(tmp_path: Path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "cores3-repair")
    calls: list[dict[str, object]] = []
    results = iter(("needs_input", "ready", "ready"))
    verifier = RepairingVerifier()
    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient(next(results), calls),
        verifier=verifier,  # type: ignore[arg-type]
    )
    try:
        job_id = builder.start("Build a timer", 1)
        wait_for(lambda: jobs.job(job_id)["state"] == "needs_input")
        jobs.focus(job_id, "layout")
        jobs.answer(job_id, "layout", "ring", "repair-answer", 2)
        builder.tick(3)
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")
        assert len(calls) == 2
        assert "independent Doodad verifier rejected" in str(calls[-1]["prompt"])
        assert [event.kind for event in jobs.events(job_id)].count("progress") == 2
    finally:
        builder.close()
        store.close()


def test_verified_output_is_outer_packaged_and_persisted_before_review_ready(
    tmp_path: Path,
) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "cores3-package")
    calls: list[dict[str, object]] = []
    results = iter(("needs_input", "ready"))
    key = bytes(range(32))
    verifier = MaterializingVerifier()
    packager = PersonalBundlePackager(
        PersonalTrustProfile("nick.local", "macbook-v0", key),
        ArtifactStore(tmp_path / "durable-artifacts"),
    )
    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient(next(results), calls),
        verifier=verifier,  # type: ignore[arg-type]
        packager=packager,
    )
    try:
        job_id = builder.start("Build a timer", 1)
        wait_for(lambda: jobs.job(job_id)["state"] == "needs_input")
        jobs.focus(job_id, "layout")
        assert jobs.answer(job_id, "layout", "ring", "package-answer", 2)
        builder.tick(3)
        wait_for(lambda: jobs.job(job_id)["state"] == "ready_for_review")

        session = store.fetch_one(
            "SELECT stage,artifact_json FROM codex_sessions WHERE job_id=?", (job_id,)
        )
        assert session["stage"] == "ready_for_review"
        artifact = json.loads(session["artifact_json"])
        assert artifact["sha256"] == package_tree_snapshot(
            Path(artifact["package_path"])
        )[0]
        bundle = artifact["bundle"]
        assert bundle["owner_id"] == "nick.local"
        assert bundle["generation_id"].startswith(
            "dev.doodad.generated-rest@0.1.0+"
        )
        bundle_path = Path(bundle["storage_path"])
        assert bundle_path.is_file()
        assert bundle_path.is_relative_to(tmp_path / "durable-artifacts")
        metadata, payload = decode_personal_bundle(bundle_path.read_bytes(), key)
        assert metadata["app_id"] == "dev.doodad.generated-rest"
        assert payload == b"\0asm-generated"
        assert jobs.events(job_id)[-1].payload["artifact"]["bundle"] == bundle
    finally:
        builder.close()
        store.close()


def test_packaging_failure_is_a_controlled_terminal_gate(tmp_path: Path) -> None:
    store = Store(tmp_path / "agent.sqlite3")
    jobs = JobManager(store, "cores3-package-fail")
    calls: list[dict[str, object]] = []
    results = iter(("needs_input", "ready"))
    builder = CodexAppBuilder(
        jobs,
        RUNTIME_ROOT,
        tmp_path / "workspaces",
        binary="unused",
        client_factory=lambda: ScriptedClient(next(results), calls),
        verifier=FakeVerifier(),  # type: ignore[arg-type]
        packager=PersonalBundlePackager(
            PersonalTrustProfile(
                "nick.local", "macbook-v0", bytes(range(32))
            ),
            ArtifactStore(tmp_path / "durable-artifacts"),
        ),
    )
    try:
        job_id = builder.start("Build a timer", 1)
        wait_for(lambda: jobs.job(job_id)["state"] == "needs_input")
        jobs.focus(job_id, "layout")
        assert jobs.answer(job_id, "layout", "ring", "package-fail-answer", 2)
        builder.tick(3)
        wait_for(lambda: jobs.job(job_id)["state"] == "failed")
        terminal = jobs.events(job_id)[-1]
        assert terminal.payload == {"reason_code": "packaging_failed"}
        session = store.fetch_one(
            "SELECT stage,artifact_json FROM codex_sessions WHERE job_id=?", (job_id,)
        )
        assert session["stage"] == "packaging"
        assert session["artifact_json"] is None
    finally:
        builder.close()
        store.close()
