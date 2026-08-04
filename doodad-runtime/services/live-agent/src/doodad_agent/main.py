"""Command-line entry point for the Phase 0-4 live-agent slice."""

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
from .capabilities import CapabilityKernel
from .controller import ForegroundController
from .fake_worker import FakeAppBuilder, ManualClock
from .jobs import JobManager
from .metrics import LatencyTrace
from .storage import Store
from .transport import DownlinkUtteranceBinding, WatchTransportServer, local_ipv4


DEFAULT_DATABASE = Path.home() / "Library/Application Support/Doodad/agent-control.sqlite3"
DEFAULT_TRACE = Path.home() / "Library/Logs/Doodad/live-agent-latency.jsonl"


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
    store, _, _, builder, attention, controller = control_plane(arguments.database)
    conversation: LiveConversation | None = None
    transport: WatchTransportServer
    downlink_binding = DownlinkUtteranceBinding()
    last_agent_state: dict[str, Any] | None = None

    async def on_audio(pcm: bytes) -> None:
        if conversation is not None:
            await conversation.feed_audio(pcm)

    async def on_event(kind: str, payload: dict[str, Any]) -> None:
        nonlocal last_agent_state
        if kind == "connected" and transport.session is not None:
            downlink_binding.cancel()
            last_agent_state = None
            if conversation is not None:
                await conversation.ready()
        elif kind == "listen.requested" and transport.session is not None:
            downlink_binding.cancel()
            if conversation is not None:
                if conversation.voice_phase == "speaking":
                    await conversation.interrupt()
                await conversation.begin_listening()
            await transport.session.start_capture()
        elif kind == "listen.finished" and transport.session is not None:
            await transport.session.stop_capture()
            if conversation is not None:
                # Feed a short bounded silence tail so VAD can close a turn
                # when the user taps Done before natural endpointing fires.
                for _ in range(25):
                    await conversation.feed_audio(b"\0\0" * 160)
        elif kind == "listen.cancelled" and transport.session is not None:
            await transport.session.stop_capture()
            downlink_binding.cancel()
            if conversation is not None:
                await conversation.cancel()
        elif kind == "capture.started":
            downlink_binding.cancel()
        elif kind == "capture.stopped" and transport.session is not None:
            trace.mark("capture.stopped", reason=payload.get("reason", "unknown"))
        elif kind == "disconnected":
            downlink_binding.cancel()
            if conversation is not None:
                conversation.disconnected()
        elif kind == "watch.state":
            controller.kernel.replace_snapshot(payload, int(time.time() * 1000))

    transport = WatchTransportServer(trace, on_audio, on_event, arguments.port)

    def audio_sink(pcm: bytes, sample_rate: int) -> int:
        return downlink_binding.enqueue(transport.session, pcm, sample_rate)

    async def stop_capture() -> None:
        if transport.session is not None:
            await transport.session.stop_capture()

    async def begin_downlink() -> None:
        downlink_binding.begin(transport.session)

    async def end_downlink() -> None:
        downlink_binding.end(transport.session)

    async def wait_for_playback() -> None:
        session = transport.session
        try:
            if session is not None:
                await session.resume_after_downlink()
        finally:
            downlink_binding.release(transport.session)

    async def invoke_action(
        capability: str, arguments: dict[str, Any], idempotency_key: str
    ) -> dict[str, Any]:
        if transport.session is None:
            raise ConnectionError("watch is not connected")
        return await transport.session.invoke_action(
            capability, arguments, idempotency_key
        )

    async def publish_state(
        voice_phase: str,
        background: dict[str, object],
        display: dict[str, str],
    ) -> None:
        nonlocal last_agent_state
        document = {
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
        if document == last_agent_state:
            return
        last_agent_state = document
        if transport.session is not None:
            await transport.session.send("agent.state", document)

    conversation = LiveConversation(
        controller, builder, attention, trace, audio_sink, stop_capture,
        begin_downlink, end_downlink, wait_for_playback, invoke_action,
        publish_state
    )
    await conversation.start()
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
        try:
            await asyncio.wait_for(conversation.close(), 18)
        except TimeoutError:
            trace.mark("shutdown.timeout", component="conversation")
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
        for event in jobs.events(jobs.store.connection.execute(
            "SELECT job_id FROM jobs ORDER BY created_at_ms LIMIT 1"
        ).fetchone()[0]):
            attention.observe(event, clock.now_ms)
        question = attention.natural_pause(clock.now_ms)
        answer = controller.fake_reply("the ring", "demo-answer")
        clock.advance(30_000)
        builder.tick(clock.now_ms)
        job_id = jobs.store.connection.execute(
            "SELECT job_id FROM jobs ORDER BY created_at_ms LIMIT 1"
        ).fetchone()[0]
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
