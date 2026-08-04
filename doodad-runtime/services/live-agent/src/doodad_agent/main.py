"""Command-line entry point for the Phase 0-5 live-agent slice."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import signal
import socket
import time
from pathlib import Path
from typing import Any

from zeroconf import IPVersion, ServiceInfo, Zeroconf

from .attention import AttentionBroker
from .builder import AppBuilder
from .capabilities import CapabilityKernel
from .codex_worker import CodexAppBuilder, default_codex_binary
from .controller import ForegroundController
from .fake_worker import FakeAppBuilder, ManualClock
from .jobs import JobManager
from .metrics import DeviceLatencyTrace, LatencyTrace
from .storage import Store
from .transport import DownlinkUtteranceBinding, WatchTransportServer, local_ipv4


DEFAULT_DATABASE = Path.home() / "Library/Application Support/Doodad/agent-control.sqlite3"
DEFAULT_TRACE = Path.home() / "Library/Logs/Doodad/live-agent-latency.jsonl"
DEFAULT_CODEX_WORKSPACES = (
    Path.home() / "Library/Application Support/Doodad/codex-jobs"
)


def find_runtime_root() -> Path:
    override = os.getenv("DOODAD_RUNTIME_ROOT")
    candidates = (
        [Path(override).expanduser()] if override else []
    ) + list(Path(__file__).resolve().parents) + list(Path.cwd().resolve().parents)
    for candidate in candidates:
        if (candidate / "doodad").is_file() and (
            candidate / "contracts"
        ).is_dir():
            return candidate.resolve()
    raise RuntimeError("cannot locate doodad-runtime; set DOODAD_RUNTIME_ROOT")


RUNTIME_ROOT = find_runtime_root()


def advertise(ip: str, port: int) -> tuple[Zeroconf, ServiceInfo]:
    service = ServiceInfo(
        "_doodad-voice._tcp.local.",
        "Doodad Live Agent._doodad-voice._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties={"path": "/ws", "v": "1", "mode": "live-agent"},
        server="doodad-live-agent.local.",
    )
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
    zeroconf.register_service(service)
    return zeroconf, service


def control_plane(database: Path, now_ms: int | None = None) -> tuple[
    Store, JobManager, CapabilityKernel, FakeAppBuilder, AttentionBroker, ForegroundController
]:
    store = Store(database)
    jobs = JobManager(store)
    jobs.recover_expired(now_ms if now_ms is not None else int(time.time() * 1000))
    kernel = CapabilityKernel(store, now_ms or 0)
    builder = FakeAppBuilder(jobs)
    attention = AttentionBroker(store, jobs)
    controller = ForegroundController(kernel, builder, attention)
    return store, jobs, kernel, builder, attention, controller


async def serve(arguments: argparse.Namespace) -> None:
    if os.getenv("DOODAD_AIORTC_DEBUG") == "1":
        logging.basicConfig(level=logging.WARNING)
        logging.getLogger("aiortc").setLevel(logging.DEBUG)
    from .conversation import LiveConversation

    trace = LatencyTrace(arguments.trace)
    store = Store(arguments.database)
    transport: WatchTransportServer

    class DeviceRuntime:
        def __init__(
            self,
            device_id: str,
            controller: ForegroundController,
            builder: AppBuilder,
            attention: AttentionBroker,
        ) -> None:
            self.device_id = device_id
            self.controller = controller
            self.builder = builder
            self.attention = attention
            self.downlink = DownlinkUtteranceBinding()
            self.last_agent_state: dict[str, Any] | None = None
            self.conversation: LiveConversation | None = None

    runtimes: dict[str, DeviceRuntime] = {}
    legacy_relinked = False

    def current_session(device_id: str):  # type: ignore[no-untyped-def]
        return transport.sessions.get(device_id)

    async def create_runtime(device_id: str) -> DeviceRuntime:
        jobs = JobManager(store, device_id)
        jobs.recover_expired(int(time.time() * 1000))
        kernel = CapabilityKernel(store, int(time.time() * 1000), device_id)
        workspace_root = Path(
            os.getenv("DOODAD_CODEX_WORKSPACE_ROOT", str(DEFAULT_CODEX_WORKSPACES))
        )
        builder = CodexAppBuilder(
            jobs,
            RUNTIME_ROOT,
            workspace_root,
            binary=default_codex_binary(),
        )
        attention = AttentionBroker(store, jobs)
        controller = ForegroundController(kernel, builder, attention)
        runtime = DeviceRuntime(device_id, controller, builder, attention)
        runtimes[device_id] = runtime
        device_trace = DeviceLatencyTrace(trace, device_id)

        def audio_sink(pcm: bytes, sample_rate: int) -> int:
            return runtime.downlink.enqueue(
                current_session(device_id), pcm, sample_rate
            )

        async def stop_capture() -> None:
            session = current_session(device_id)
            if session is not None:
                await session.stop_capture()

        async def begin_downlink() -> None:
            runtime.downlink.begin(current_session(device_id))

        async def end_downlink() -> None:
            runtime.downlink.end(current_session(device_id))

        async def wait_for_playback() -> None:
            session = current_session(device_id)
            try:
                if session is not None:
                    await session.resume_after_downlink()
            finally:
                runtime.downlink.release(current_session(device_id))

        async def invoke_action(
            capability: str, action_arguments: dict[str, Any], idempotency_key: str
        ) -> dict[str, Any]:
            session = current_session(device_id)
            if session is None:
                raise ConnectionError(f"{device_id} is not connected")
            return await session.invoke_action(
                capability, action_arguments, idempotency_key
            )

        async def publish_state(
            voice_phase: str,
            background: dict[str, object],
            display: dict[str, str],
        ) -> None:
            document = {
                "device_id": device_id,
                "voice_phase": voice_phase,
                "display": {
                    "transcript": str(display.get("transcript", ""))[:160],
                    "response": str(display.get("response", ""))[:160],
                },
                "background": {
                    "running_count": int(background.get("running_count", 0)),
                    "focused_question": background.get("focused_question") is not None,
                    "review_ready": bool(background.get("review_ready", False)),
                    "completion_pending": bool(background.get("completion_pending", 0)),
                    "install_state": 0,
                },
            }
            if document == runtime.last_agent_state:
                return
            runtime.last_agent_state = document
            session = current_session(device_id)
            if session is not None:
                await session.send("agent.state", document)

        runtime.conversation = LiveConversation(
            controller, builder, attention, device_trace, audio_sink, stop_capture,
            begin_downlink, end_downlink, wait_for_playback, invoke_action,
            publish_state,
        )
        await runtime.conversation.start()
        return runtime

    async def on_audio(device_id: str, pcm: bytes) -> None:
        runtime = runtimes.get(device_id)
        if runtime is not None and runtime.conversation is not None:
            await runtime.conversation.feed_audio(pcm)

    async def on_event(
        device_id: str, kind: str, payload: dict[str, Any]
    ) -> None:
        nonlocal legacy_relinked
        if kind == "identified" and device_id.startswith("cores3-") and not legacy_relinked:
            store.relink_legacy_device(device_id)
            legacy_relinked = True
        runtime = runtimes.get(device_id)
        if runtime is None:
            runtime = await create_runtime(device_id)
        conversation = runtime.conversation
        session = current_session(device_id)
        if kind == "connected" and session is not None:
            runtime.downlink.cancel()
            runtime.last_agent_state = None
            if conversation is not None:
                await conversation.ready()
        elif kind == "listen.requested" and session is not None:
            runtime.downlink.cancel()
            if conversation is not None:
                if conversation.voice_phase == "speaking":
                    await conversation.interrupt()
                await conversation.begin_listening()
            await session.start_capture()
        elif kind == "listen.finished" and session is not None:
            await session.stop_capture()
            if conversation is not None:
                for _ in range(25):
                    await conversation.feed_audio(b"\0\0" * 160)
        elif kind == "listen.cancelled" and session is not None:
            await session.stop_capture()
            runtime.downlink.cancel()
            if conversation is not None:
                await conversation.cancel()
        elif kind == "capture.started":
            runtime.downlink.cancel()
        elif kind == "capture.stopped":
            trace.mark(
                "capture.stopped", device_id=device_id,
                reason=payload.get("reason", "unknown"),
            )
        elif kind == "disconnected":
            runtime.downlink.cancel()
            if conversation is not None:
                conversation.disconnected()
        elif kind == "watch.state":
            runtime.controller.kernel.replace_snapshot(
                payload, int(time.time() * 1000)
            )

    transport = WatchTransportServer(trace, on_audio, on_event, arguments.port)
    await transport.start()
    ip = local_ipv4()
    zeroconf, service = await asyncio.to_thread(advertise, ip, arguments.port)
    print(f"Doodad Live Agent listening at ws://{ip}:{arguments.port}/ws", flush=True)
    print("Foreground model and provider keys loaded (values hidden).", flush=True)
    stopped = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, stopped.set)
        except NotImplementedError:
            pass
    try:
        await stopped.wait()
    finally:
        trace.mark("shutdown.started")
        try:
            await asyncio.wait_for(
                asyncio.to_thread(zeroconf.unregister_service, service), 3
            )
        except TimeoutError:
            trace.mark("shutdown.timeout", component="zeroconf.unregister")
        try:
            await asyncio.wait_for(asyncio.to_thread(zeroconf.close), 3)
        except TimeoutError:
            trace.mark("shutdown.timeout", component="zeroconf.close")
        try:
            await asyncio.wait_for(transport.close(), 8)
        except TimeoutError:
            trace.mark("shutdown.timeout", component="transport")
        for device_id, runtime in runtimes.items():
            if runtime.conversation is None:
                continue
            try:
                await asyncio.wait_for(runtime.conversation.close(), 18)
            except TimeoutError:
                trace.mark(
                    "shutdown.timeout", component="conversation",
                    device_id=device_id,
                )
        store.close()
        trace.mark("shutdown.completed")


def check_config() -> int:
    required = ("OPENAI_API_KEY", "ELEVENLABS_API_KEY", "ELEVENLABS_DEFAULT_VOICE_ID")
    optional = (
        "OPENAI_FOREGROUND_MODEL",
        "OPENAI_STT_MODEL",
        "DOODAD_ELEVENLABS_MODEL_ID",
        "DOODAD_MAX_COMPLETION_TOKENS",
        "DOODAD_MAX_RESPONSE_TEXT_BYTES",
        "DOODAD_DOWNLINK_MAX_SPOOL_SECONDS",
        "DOODAD_CODEX_BINARY",
        "DOODAD_CODEX_WORKSPACE_ROOT",
        "DOODAD_RUNTIME_ROOT",
    )
    result = {
        "ready": all(bool(os.getenv(name)) for name in required),
        "required": {name: bool(os.getenv(name)) for name in required},
        "optional_overrides": {name: bool(os.getenv(name)) for name in optional},
    }
    print(json.dumps(result, indent=2))
    return 0 if result["ready"] else 2


def fake_demo(database: Path) -> int:
    clock = ManualClock(1_000)
    store = Store(database)
    try:
        jobs = JobManager(store)
        kernel = CapabilityKernel(store, clock.now_ms)
        builder = FakeAppBuilder(jobs)
        attention = AttentionBroker(store, jobs)
        controller = ForegroundController(
            kernel, builder, attention, now_ms=lambda: clock.now_ms
        )
        started = controller.fake_reply("Build me a rest timer app", "demo-build")
        foreground = controller.fake_reply("What is my next set?", "demo-next")
        clock.advance(10_000)
        builder.tick(clock.now_ms)
        for event in jobs.events(jobs.store.fetch_one(
            "SELECT job_id FROM jobs ORDER BY created_at_ms LIMIT 1"
        )[0]):
            attention.observe(event, clock.now_ms)
        question = attention.natural_pause(clock.now_ms)
        answer = controller.fake_reply("the ring", "demo-answer")
        clock.advance(30_000)
        builder.tick(clock.now_ms)
        job_id = jobs.store.fetch_one(
            "SELECT job_id FROM jobs ORDER BY created_at_ms LIMIT 1"
        )[0]
        for event in jobs.events(job_id):
            attention.observe(event, clock.now_ms)
        completion = attention.natural_pause(clock.now_ms)
        print(json.dumps({
            "started": started,
            "foreground_during_job": foreground,
            "question": None if question is None else question.text,
            "answer": answer,
            "completion": None if completion is None else completion.text,
            "state": jobs.job(job_id)["state"],
        }, indent=2))
        return 0
    finally:
        store.close()


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="doodad-live-agent")
    subparsers = parser.add_subparsers(dest="command", required=True)
    serve_parser = subparsers.add_parser("serve")
    serve_parser.add_argument("--port", type=int, default=8765)
    serve_parser.add_argument("--database", type=Path, default=DEFAULT_DATABASE)
    serve_parser.add_argument("--trace", type=Path, default=DEFAULT_TRACE)
    demo_parser = subparsers.add_parser("fake-demo")
    demo_parser.add_argument("--database", type=Path, required=True)
    subparsers.add_parser("check-config")
    return parser.parse_args(argv)


def cli(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    if arguments.command == "check-config":
        return check_config()
    if arguments.command == "fake-demo":
        return fake_demo(arguments.database)
    asyncio.run(serve(arguments))
    return 0


if __name__ == "__main__":
    raise SystemExit(cli())
