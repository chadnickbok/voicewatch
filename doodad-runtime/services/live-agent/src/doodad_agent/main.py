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
from .app_delivery import AppArtifactServer, AppReadyPublisher
from .builder import AppBuilder, CompositeBuilder
from .capabilities import CapabilityKernel
from .codex_worker import CodexAppBuilder, default_codex_binary
from .codex_work_worker import CodexWorkBuilder, SmtpDeliveryProvider
from .controller import ForegroundController
from .fake_worker import FakeAppBuilder, ManualClock
from .fake_work_worker import FakeWorkBuilder
from .jobs import JobManager
from .metrics import DeviceLatencyTrace, LatencyTrace
from .personal_bundle import (
    ArtifactStore,
    PersonalBundleError,
    PersonalBundlePackager,
    PersonalTrustProfile,
)
from .storage import Store
from .session import DownlinkUtteranceBinding
from .provider_session import ProviderSession
from .host_network import local_ipv4


DEFAULT_DATABASE = Path.home() / "Library/Application Support/Doodad/agent-control.sqlite3"
DEFAULT_TRACE = Path.home() / "Library/Logs/Doodad/live-agent-latency.jsonl"
DEFAULT_CODEX_WORKSPACES = (
    Path.home() / "Library/Application Support/Doodad/codex-jobs"
)
DEFAULT_PERSONAL_ARTIFACTS = (
    Path.home() / "Library/Application Support/Doodad/personal-apps"
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


def personal_trust_from_environment() -> PersonalTrustProfile | None:
    """Enable personal delivery only with one complete explicit trust profile."""

    owner_id = os.getenv("DOODAD_PERSONAL_OWNER_ID", "").strip()
    key_hex = os.getenv("DOODAD_PERSONAL_HMAC_KEY_HEX", "").strip()
    if not owner_id and not key_hex:
        return None
    if not owner_id or not key_hex:
        raise PersonalBundleError(
            "DOODAD_PERSONAL_OWNER_ID and DOODAD_PERSONAL_HMAC_KEY_HEX "
            "must be configured together"
        )
    signer_key_id = (
        os.getenv("DOODAD_PERSONAL_SIGNER_KEY_ID", "personal-v1").strip()
        or "personal-v1"
    )
    return PersonalTrustProfile.from_hex(owner_id, signer_key_id, key_hex)


def advertise(ip: str, port: int, transport: str = 'webrtc') -> tuple[Zeroconf, ServiceInfo]:
    service = ServiceInfo(
        "_doodad-voice._tcp.local.",
        ("Doodad MoQ Live Agent" if transport == 'moq' else "Doodad Live Agent") + "._doodad-voice._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties=({"path": "/v1/moq/control", "bootstrap": "/v1/moq/bootstrap",
                     "v": "1", "mode": "live-agent", "transport": "moq-lite-05", "tls": "1"}
                    if transport == 'moq' else {"path": "/ws", "v": "1", "mode": "live-agent"}),
        server="doodad-live-agent.local.",
    )
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
    zeroconf.register_service(service)
    return zeroconf, service


def control_plane(database: Path, now_ms: int | None = None) -> tuple[
    Store, JobManager, CapabilityKernel, AppBuilder, AttentionBroker, ForegroundController
]:
    store = Store(database)
    jobs = JobManager(store)
    jobs.recover_expired(now_ms if now_ms is not None else int(time.time() * 1000))
    kernel = CapabilityKernel(store, now_ms or 0)
    builder = CompositeBuilder(FakeAppBuilder(jobs), FakeWorkBuilder(jobs))
    attention = AttentionBroker(store, jobs)
    controller = ForegroundController(kernel, builder, attention)
    return store, jobs, kernel, builder, attention, controller


async def complete_capture_to_conversation(conversation, session, event: str) -> None:
    """Commit explicit PTT only after validated MoQ PCM; retain legacy VAD padding."""
    completed_event = 'capture.stopped' if getattr(session, 'explicit_capture_completion', False) else 'listen.finished'
    if conversation is not None and event == completed_event:
        if getattr(session, 'explicit_capture_completion', False):
            await conversation.capture_completed()
        else:
            for _ in range(25):
                await conversation.feed_audio(b'\0\0' * 160)


async def cancel_capture_to_conversation(conversation, session) -> None:
    """Fence MoQ provider work before awaiting the device's stop operation."""
    explicit = getattr(session, 'explicit_capture_completion', False)
    if explicit and conversation is not None:
        await conversation.cancel()
    await session.stop_capture()
    if not explicit and conversation is not None:
        await conversation.cancel()


async def serve(arguments: argparse.Namespace) -> None:
    moq_config = None
    if arguments.transport == 'moq':
        from .moq_config import MoqHostConfig
        from .transport_moq import MoqTransportServer
        moq_config = MoqHostConfig.load(arguments.moq_config)
    else:
        from .transport_webrtc import WatchTransportServer

    personal_trust = personal_trust_from_environment()
    if os.getenv("DOODAD_AIORTC_DEBUG") == "1":
        logging.basicConfig(level=logging.WARNING)
        logging.getLogger("aiortc").setLevel(logging.DEBUG)
    from .conversation import LiveConversation

    trace = LatencyTrace(arguments.trace)
    store = Store(arguments.database)
    ip = local_ipv4()
    artifact_store = (
        ArtifactStore(
            Path(
                os.getenv(
                    "DOODAD_PERSONAL_ARTIFACT_ROOT",
                    str(DEFAULT_PERSONAL_ARTIFACTS),
                )
                or str(DEFAULT_PERSONAL_ARTIFACTS)
            )
        )
        if personal_trust is not None
        else None
    )
    packager = (
        PersonalBundlePackager(personal_trust, artifact_store)
        if personal_trust is not None and artifact_store is not None
        else None
    )
    artifact_server = (
        AppArtifactServer(artifact_store) if artifact_store is not None else None
    )
    app_publisher = (
        AppReadyPublisher(
            (f"https://{moq_config.public_host}:{arguments.port}" if moq_config is not None
             else f"http://{ip}:{arguments.port}"),
            artifact_store,
            owner_id=personal_trust.owner_id,
            signer_key_id=personal_trust.signer_key_id,
        )
        if artifact_store is not None and personal_trust is not None
        else None
    )

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
            self.last_app_publish_at = 0.0
            self.conversation: LiveConversation | None = None
            self.provider_session: ProviderSession | None = None
            self.conversation_lock = asyncio.Lock()

    runtimes: dict[str, DeviceRuntime] = {}
    legacy_relinked = False

    def current_session(device_id: str):  # type: ignore[no-untyped-def]
        return transport.sessions.get(device_id)

    async def publish_ready_apps(
        runtime: DeviceRuntime, *, force: bool = False
    ) -> None:
        if app_publisher is None:
            return
        session = current_session(runtime.device_id)
        if session is None:
            return
        now = asyncio.get_running_loop().time()
        if not force and now - runtime.last_app_publish_at < 1.0:
            return
        runtime.last_app_publish_at = now
        announced = await app_publisher.publish_pending(
            session, store, runtime.device_id
        )
        for digest in announced:
            trace.mark(
                "app.ready", device_id=runtime.device_id,
                bundle_sha256=digest,
            )

    async def create_runtime(device_id: str) -> DeviceRuntime:
        jobs = JobManager(store, device_id)
        jobs.recover_expired(int(time.time() * 1000))
        kernel = CapabilityKernel(store, int(time.time() * 1000), device_id)
        workspace_root = Path(
            os.getenv("DOODAD_CODEX_WORKSPACE_ROOT", str(DEFAULT_CODEX_WORKSPACES))
        )
        app_builder = CodexAppBuilder(
            jobs,
            RUNTIME_ROOT,
            workspace_root,
            binary=default_codex_binary(),
            packager=packager,
        )
        work_builder = CodexWorkBuilder(
            jobs,
            workspace_root,
            default_codex_binary(),
            delivery=SmtpDeliveryProvider.from_environment(),
        )
        builder = CompositeBuilder(app_builder, work_builder)
        attention = AttentionBroker(store, jobs)
        controller = ForegroundController(kernel, builder, attention)
        runtime = DeviceRuntime(device_id, controller, builder, attention)
        runtimes[device_id] = runtime
        await start_conversation(runtime)
        return runtime

    async def start_conversation(runtime: DeviceRuntime) -> None:
        async with runtime.conversation_lock:
            await replace_conversation(runtime)

    async def replace_conversation(runtime: DeviceRuntime) -> None:
        device_id = runtime.device_id
        session = current_session(device_id)
        if runtime.provider_session is not None and runtime.provider_session.session is session:
            return
        history = []
        if runtime.provider_session is not None:
            runtime.provider_session.retire()
        if runtime.conversation is not None:
            await runtime.conversation.close(close_builder=False)
            history = runtime.conversation.history()
        owner = ProviderSession(session, lambda: current_session(device_id))
        runtime.provider_session = owner
        runtime.downlink = owner.downlink
        device_trace = DeviceLatencyTrace(trace, device_id)

        async def publish_state(
            voice_phase: str,
            background: dict[str, object],
            display: dict[str, str],
        ) -> None:
            if not owner.live():
                return
            document = {
                "schema_version": 1,
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
                    "status_changed": bool(background.get("status_changed", False)),
                    "install_state": 0,
                    "tasks": list(background.get("tasks", []))[:3],
                },
            }
            changed = document != runtime.last_agent_state
            session = owner.session
            if changed:
                runtime.last_agent_state = document
            if changed and session is not None:
                await session.send("agent.state", document)
            if document["background"]["review_ready"]:
                await publish_ready_apps(runtime, force=changed)

        runtime.conversation = LiveConversation(
            runtime.controller, runtime.builder, runtime.attention, device_trace,
            owner.audio, owner.stop_capture, owner.begin, owner.end, owner.wait, owner.action,
            publish_state, history=history,
            explicit_capture=getattr(session, 'explicit_capture_completion', False),
            authorize_response=owner.authorize_response if getattr(session, 'explicit_capture_completion', False) else None,
        )
        await runtime.conversation.start()

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
        elif kind == 'identified':
            await start_conversation(runtime)
        conversation = runtime.conversation
        session = current_session(device_id)
        if kind == "connected" and session is not None:
            runtime.downlink.cancel()
            runtime.last_agent_state = None
            runtime.last_app_publish_at = 0.0
            await publish_ready_apps(runtime, force=True)
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
            await complete_capture_to_conversation(conversation, session, kind)
        elif kind == "listen.cancelled" and session is not None:
            runtime.downlink.cancel()
            await cancel_capture_to_conversation(conversation, session)
        elif kind == "conversation.text" and conversation is not None:
            text = payload.get("text")
            if isinstance(text, str):
                runtime.downlink.cancel()
                if conversation.voice_phase == "speaking":
                    await conversation.interrupt()
                await conversation.submit_text(text)
        elif kind == "capture.started":
            runtime.downlink.cancel()
            if conversation is not None:
                await conversation.capture_started()
        elif kind == "capture.stopped":
            trace.mark(
                "capture.stopped", device_id=device_id,
                reason=payload.get("reason", "unknown"),
            )
            await complete_capture_to_conversation(conversation, session, kind)
        elif kind == "capture.failed":
            runtime.downlink.cancel()
            if conversation is not None:
                await conversation.cancel()
        elif kind == "disconnected":
            runtime.downlink.cancel()
            if runtime.provider_session is not None:
                runtime.provider_session.retire()
            if conversation is not None:
                conversation.disconnected()
        elif kind == "watch.state":
            runtime.controller.kernel.replace_snapshot(
                payload, int(time.time() * 1000)
            )

    if moq_config is not None:
        transport = MoqTransportServer(
            trace, on_audio, on_event, arguments.port,
            registry=moq_config.registry, context=moq_config.context,
            ipc_path=moq_config.ipc_path, media_host=moq_config.public_host,
            media_port=moq_config.media_port, artifact_server=artifact_server,
            time_port=moq_config.time_port,
        )
    else:
        transport = WatchTransportServer(
            trace, on_audio, on_event, arguments.port,
            artifact_server=artifact_server,
        )
    await transport.start()
    zeroconf, service = await asyncio.to_thread(advertise, ip, arguments.port, arguments.transport)
    print("Doodad Live Agent listening with authenticated MoQ/WSS" if moq_config is not None
          else f"Doodad Live Agent listening at ws://{ip}:{arguments.port}/ws", flush=True)
    print("Foreground model and provider keys loaded (values hidden).", flush=True)
    print(
        "Personal app delivery enabled."
        if personal_trust is not None
        else "Personal app delivery disabled; owner and key are not configured.",
        flush=True,
    )
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
        "DOODAD_PERSONAL_OWNER_ID",
        "DOODAD_PERSONAL_SIGNER_KEY_ID",
        "DOODAD_PERSONAL_HMAC_KEY_HEX",
        "DOODAD_PERSONAL_ARTIFACT_ROOT",
        "DOODAD_SMTP_HOST",
        "DOODAD_SMTP_PORT",
        "DOODAD_SMTP_SENDER",
        "DOODAD_SMTP_USERNAME",
        "DOODAD_SMTP_PASSWORD",
    )
    try:
        personal_profile = personal_trust_from_environment()
        personal_valid = True
        personal_error = None
    except PersonalBundleError as error:
        personal_profile = None
        personal_valid = False
        personal_error = str(error)
    result = {
        "ready": all(bool(os.getenv(name)) for name in required) and personal_valid,
        "required": {name: bool(os.getenv(name)) for name in required},
        "optional_overrides": {name: bool(os.getenv(name)) for name in optional},
        "personal_app_delivery": {
            "enabled": personal_profile is not None,
            "valid": personal_valid,
            "error": personal_error,
        },
    }
    print(json.dumps(result, indent=2))
    return 0 if result["ready"] else 2


def fake_demo(database: Path) -> int:
    clock = ManualClock(1_000)
    store = Store(database)
    try:
        jobs = JobManager(store)
        kernel = CapabilityKernel(store, clock.now_ms)
        builder = CompositeBuilder(FakeAppBuilder(jobs), FakeWorkBuilder(jobs))
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
    serve_parser.add_argument('--transport', choices=('webrtc', 'moq'), default='webrtc')
    serve_parser.add_argument('--moq-config', type=Path, help='owner-private MoQ host JSON configuration')
    demo_parser = subparsers.add_parser("fake-demo")
    demo_parser.add_argument("--database", type=Path, required=True)
    subparsers.add_parser("check-config")
    arguments = parser.parse_args(argv)
    if arguments.command == 'serve':
        if arguments.transport == 'moq' and arguments.moq_config is None:
            parser.error('--transport moq requires --moq-config')
        if arguments.transport != 'moq' and arguments.moq_config is not None:
            parser.error('--moq-config requires --transport moq')
    return arguments


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
