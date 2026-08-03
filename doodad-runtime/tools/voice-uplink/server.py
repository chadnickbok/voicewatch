#!/usr/bin/env python3
"""Local WebRTC receiver and physical voice-uplink conformance harness."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import re
import socket
import subprocess
import sys
import time
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from aiohttp import WSMsgType, web
from aiortc import RTCPeerConnection, RTCSessionDescription
from av.audio.resampler import AudioResampler
from zeroconf import IPVersion, ServiceInfo, Zeroconf

from protocol import envelope, word_error_rate


ROOT = Path(__file__).resolve().parent
DEFAULT_PHRASE = "Doodad voice streaming works from watch to Mac."


def mac_output_settings() -> tuple[int, bool]:
    result = subprocess.run(
        [
            "osascript",
            "-e",
            'set s to get volume settings',
            "-e",
            'return (output volume of s as string) & "," & (output muted of s as string)',
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    volume, muted = result.split(",", 1)
    return int(volume), muted == "true"


def set_mac_output(volume: int, muted: bool) -> None:
    mute_clause = "with output muted" if muted else "without output muted"
    subprocess.run(
        [
            "osascript",
            "-e",
            f"set volume output volume {volume} {mute_clause}",
        ],
        check=True,
    )


def local_ipv4() -> str:
    if sys.platform == "darwin":
        wifi = subprocess.run(
            ["ipconfig", "getifaddr", "en0"],
            check=False,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if wifi:
            return wifi
        route = subprocess.run(
            ["route", "-n", "get", "default"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        match = re.search(r"^\s*interface:\s*(\S+)", route, re.MULTILINE)
        if match is not None:
            address = subprocess.run(
                ["ipconfig", "getifaddr", match.group(1)],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            if address:
                return address
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        return probe.getsockname()[0]
    finally:
        probe.close()


def keep_host_candidate(sdp: str, address: str) -> str:
    """Expose only the Wi-Fi host candidate the watch can actually reach."""
    filtered: list[str] = []
    kept_candidate = False
    for line in sdp.splitlines():
        if not line.startswith("a=candidate:"):
            filtered.append(line)
        elif not kept_candidate and f" {address} " in line:
            filtered.append(line)
            kept_candidate = True
    return "\r\n".join(filtered) + "\r\n"


class SegmentRecorder:
    def __init__(self) -> None:
        self._track = None
        self._task: asyncio.Task[None] | None = None
        self._writer: wave.Wave_write | None = None
        self._lock = asyncio.Lock()
        self._resampler = AudioResampler(format="s16", layout="mono", rate=48_000)
        self.frames_received = 0
        self.samples_written = 0

    async def attach(self, track: Any) -> None:
        self._track = track
        if self._task is None:
            self._task = asyncio.create_task(self._consume())
            self._task.add_done_callback(self._report_failure)

    async def start(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        writer = wave.open(str(path), "wb")
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(48_000)
        async with self._lock:
            if self._writer is not None:
                self._writer.close()
            self._writer = writer
            self.frames_received = 0
            self.samples_written = 0

    async def stop(self) -> None:
        async with self._lock:
            if self._writer is not None:
                self._writer.close()
                self._writer = None

    async def close(self) -> None:
        await self.stop()
        if self._task is not None:
            self._task.cancel()
            await asyncio.gather(self._task, return_exceptions=True)
            self._task = None

    async def _consume(self) -> None:
        assert self._track is not None
        while True:
            frame = await self._track.recv()
            self.frames_received += 1
            # We preserve receipt order and capture wall time in the WAV. The
            # ESP peer's RTP timestamps may contain gaps while its encoder is
            # under load, which libav's stateful resampler otherwise treats as
            # a discontinuity and buffers instead of returning audio.
            frame.pts = None
            for converted in self._resampler.resample(frame):
                # PyAV exposes packed mono s16 as a single plane. Read only
                # the actual samples, excluding any SIMD-alignment padding,
                # without adding NumPy to this small conformance harness.
                samples = bytes(converted.planes[0])[: converted.samples * 2]
                async with self._lock:
                    if self._writer is not None:
                        self._writer.writeframesraw(samples)
                        self.samples_written += len(samples) // 2

    def _report_failure(self, task: asyncio.Task[None]) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None:
            print(f"audio recorder failed: {error!r}", flush=True)


@dataclass
class RunResult:
    run: int
    wav: str
    transcript: str
    wer: float
    elapsed_ms: int
    encoded_frames: int
    received_frames: int
    dropped_frames: int


class LabSession:
    def __init__(self, websocket: web.WebSocketResponse, arguments: argparse.Namespace) -> None:
        self.websocket = websocket
        self.arguments = arguments
        self.peer = RTCPeerConnection()
        self.sequence = 0
        self.messages: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.connected = asyncio.Event()
        self.recorder = SegmentRecorder()
        self.results: list[RunResult] = []
        self.runner: asyncio.Task[None] | None = None
        self.sdp_chunks: list[str | None] | None = None

        @self.peer.on("track")
        async def on_track(track: Any) -> None:
            if track.kind == "audio":
                print("audio track attached", flush=True)
                await self.recorder.attach(track)

        @self.peer.on("connectionstatechange")
        async def on_connectionstatechange() -> None:
            print(f"peer state: {self.peer.connectionState}", flush=True)
            if self.peer.connectionState == "connected":
                self.connected.set()

    async def send(self, message_type: str, payload: dict[str, Any] | None = None) -> None:
        self.sequence += 1
        await self.websocket.send_str(envelope(message_type, self.sequence, payload))

    async def receive(self, message: dict[str, Any]) -> None:
        if message.get("v") != 1 or not isinstance(message.get("type"), str):
            return
        message_type = message["type"]
        print(f"signal <- {message_type}", flush=True)
        if message_type == "hello":
            await self.send("welcome", {"mode": "sendonly-audio"})
        elif message_type == "sdp":
            payload = message.get("payload") or {}
            if payload.get("kind") != "offer" or not isinstance(payload.get("sdp"), str):
                return
            await self.accept_offer(payload["sdp"])
        elif message_type == "sdp.chunk":
            payload = message.get("payload") or {}
            index = payload.get("index")
            count = payload.get("count")
            data = payload.get("data")
            if (
                payload.get("kind") != "offer"
                or not isinstance(index, int)
                or not isinstance(count, int)
                or not isinstance(data, str)
                or count < 1
                or count > 256
                or index < 0
                or index >= count
            ):
                return
            if self.sdp_chunks is None or len(self.sdp_chunks) != count:
                self.sdp_chunks = [None] * count
            self.sdp_chunks[index] = data
            if all(chunk is not None for chunk in self.sdp_chunks):
                offer = "".join(chunk or "" for chunk in self.sdp_chunks)
                self.sdp_chunks = None
                await self.accept_offer(offer)
        elif message_type == "ice":
            # esp_peer normally gathers host candidates into SDP on this LAN.
            # A future TURN-capable profile will add structured trickle ICE.
            pass
        else:
            await self.messages.put(message)

    async def accept_offer(self, sdp: str) -> None:
        (self.arguments.artifacts / "offer.sdp").write_text(sdp, encoding="utf-8")
        await self.peer.setRemoteDescription(
            RTCSessionDescription(sdp=sdp, type="offer")
        )
        answer = await self.peer.createAnswer()
        await self.peer.setLocalDescription(answer)
        assert self.peer.localDescription is not None
        answer_sdp = keep_host_candidate(
            self.peer.localDescription.sdp, local_ipv4())
        (self.arguments.artifacts / "answer.sdp").write_text(
            answer_sdp, encoding="utf-8"
        )
        await self.send("sdp", {
            "kind": "answer",
            "sdp": answer_sdp,
        })
        print("signal -> sdp answer", flush=True)

    async def wait_for(self, message_type: str, timeout: float) -> dict[str, Any]:
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            message = await asyncio.wait_for(self.messages.get(), remaining)
            if message.get("type") == message_type:
                return message

    async def run_lab(self) -> None:
        await asyncio.wait_for(self.connected.wait(), 30)
        await asyncio.sleep(0.5)
        for run_number in range(1, self.arguments.runs + 1):
            directory = self.arguments.artifacts / f"run-{run_number:02d}"
            directory.mkdir(parents=True, exist_ok=True)
            wav_path = directory / "watch-uplink.wav"
            await self.recorder.start(wav_path)
            started = time.monotonic()
            await self.send("capture.start", {"duration_ms": self.arguments.duration_ms})
            await asyncio.sleep(0.6)
            prior_volume, prior_muted = mac_output_settings()
            try:
                set_mac_output(self.arguments.speaker_volume, False)
                player = await asyncio.create_subprocess_exec(
                    "afplay", str(self.arguments.phrase_audio)
                )
                await player.wait()
            finally:
                set_mac_output(prior_volume, prior_muted)
            stopped = await self.wait_for(
                "capture.stopped",
                timeout=self.arguments.duration_ms / 1000 + 5,
            )
            await asyncio.sleep(0.5)
            await self.recorder.stop()
            if self.recorder.samples_written == 0:
                print(
                    f"decoder delivered {self.recorder.frames_received} frames",
                    flush=True,
                )
                await self.print_peer_stats()
                raise RuntimeError("WebRTC connected but no decoded audio frames arrived")
            transcript = await transcribe(wav_path, self.arguments.model, directory)
            received_frames = self.recorder.samples_written // 960
            capture = stopped.get("payload") or {}
            result = RunResult(
                run=run_number,
                wav=str(wav_path),
                transcript=transcript,
                wer=word_error_rate(self.arguments.phrase, transcript),
                elapsed_ms=int(capture.get(
                    "elapsed_ms", (time.monotonic() - started) * 1000
                )),
                encoded_frames=int(capture.get("encoded_frames", 0)),
                received_frames=received_frames,
                dropped_frames=int(capture.get("dropped_frames", 0)),
            )
            self.results.append(result)
            (directory / "result.json").write_text(
                json.dumps(result.__dict__, indent=2) + "\n", encoding="utf-8"
            )
            await self.send("transcript.final", {"text": transcript})
            print(
                f"run {run_number}/{self.arguments.runs}: "
                f"{result.encoded_frames} encoded, "
                f"{result.received_frames} received, "
                f"{result.dropped_frames} dropped, "
                f"WER={result.wer:.3f}, transcript={transcript!r}",
                flush=True,
            )
            await asyncio.sleep(1)
        summary = {
            "phrase": self.arguments.phrase,
            "runs": [result.__dict__ for result in self.results],
            "mean_wer": sum(result.wer for result in self.results) / len(self.results),
            "passed": all(
                result.encoded_frames > 0 and result.received_frames > 0
                for result in self.results
            ),
        }
        (self.arguments.artifacts / "summary.json").write_text(
            json.dumps(summary, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps(summary, indent=2), flush=True)
        # End the request handler as soon as the finite lab run is complete.
        # Otherwise aiohttp's runner cleanup waits for the watch to close its
        # persistent signaling socket, making an unattended one-shot run hang.
        await self.websocket.close(code=1000, message=b"lab complete")

    async def print_peer_stats(self) -> None:
        stats = await self.peer.getStats()
        for report in stats.values():
            if report.type in {"inbound-rtp", "transport", "candidate-pair"}:
                print(f"WebRTC stats: {report}", flush=True)

    async def close(self) -> None:
        if self.runner is not None and not self.runner.done():
            self.runner.cancel()
            await asyncio.gather(self.runner, return_exceptions=True)
        await self.recorder.close()
        await self.peer.close()


async def transcribe(wav_path: Path, model: Path, directory: Path) -> str:
    output_base = directory / "whisper"
    process = await asyncio.create_subprocess_exec(
        "whisper-cli",
        "-m", str(model),
        "-f", str(wav_path),
        "-otxt",
        "-of", str(output_base),
        "-nt",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
    )
    output, _ = await process.communicate()
    (directory / "whisper.log").write_bytes(output)
    if process.returncode != 0:
        raise RuntimeError(f"whisper-cli failed with exit code {process.returncode}")
    transcript_path = output_base.with_suffix(".txt")
    return transcript_path.read_text(encoding="utf-8").strip()


async def websocket_handler(request: web.Request) -> web.WebSocketResponse:
    state = request.app["state"]
    arguments = state["arguments"]
    # The watch controls liveness and reconnects itself. Avoid server-originated
    # ping deadlines during the ESP32's CPU-heavy ICE/DTLS setup.
    websocket = web.WebSocketResponse(max_msg_size=16 * 1024)
    await websocket.prepare(request)
    previous: LabSession | None = state["session"]
    if previous is not None:
        await previous.close()
    session = LabSession(websocket, arguments)
    state["session"] = session
    print(f"watch connected from {request.remote}", flush=True)
    try:
        async for message in websocket:
            if message.type == WSMsgType.TEXT:
                try:
                    await session.receive(json.loads(message.data))
                    if session.connected.is_set() and session.runner is None:
                        session.runner = asyncio.create_task(session.run_lab())
                        session.runner.add_done_callback(
                            lambda task: report_runner_result(task, state["done"])
                        )
                except (json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
                    print(f"discarded signaling message: {error}", flush=True)
            elif message.type == WSMsgType.ERROR:
                break
    finally:
        await session.close()
        if state["session"] is session:
            state["session"] = None
    return websocket


def report_runner_result(task: asyncio.Task[None], done: asyncio.Event) -> None:
    if task.cancelled():
        done.set()
        return
    error = task.exception()
    if error is not None:
        print(f"lab failed: {error}", flush=True)
    done.set()


def make_phrase_audio(phrase: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["say", "-v", "Samantha", "-r", "165", "-o", str(path), phrase], check=True)


def advertise(ip: str, port: int) -> tuple[Zeroconf, ServiceInfo]:
    service = ServiceInfo(
        "_doodad-voice._tcp.local.",
        "Echo Bridge._doodad-voice._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties={"path": "/ws", "v": "1"},
        server="doodad-voice.local.",
    )
    zeroconf = Zeroconf(ip_version=IPVersion.V4Only)
    zeroconf.register_service(service)
    return zeroconf, service


async def main(arguments: argparse.Namespace) -> None:
    if arguments.debug_rtp:
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter("%(name)s: %(message)s"))
        receiver_log = logging.getLogger("aiortc.rtcrtpreceiver")
        receiver_log.addHandler(handler)
        receiver_log.setLevel(logging.DEBUG)
        receiver_log.propagate = False
    arguments.artifacts.mkdir(parents=True, exist_ok=True)
    make_phrase_audio(arguments.phrase, arguments.phrase_audio)
    application = web.Application()
    done = asyncio.Event()
    application["state"] = {
        "arguments": arguments,
        "session": None,
        "done": done,
    }
    application.router.add_get("/ws", websocket_handler)
    runner = web.AppRunner(application)
    await runner.setup()
    site = web.TCPSite(runner, "0.0.0.0", arguments.port)
    await site.start()
    ip = local_ipv4()
    zeroconf, service = await asyncio.to_thread(
        advertise, ip, arguments.port)
    print(f"Echo Bridge listening at ws://{ip}:{arguments.port}/ws", flush=True)
    print(f"test phrase: {arguments.phrase!r}", flush=True)
    try:
        await done.wait()
    finally:
        await asyncio.to_thread(zeroconf.unregister_service, service)
        await asyncio.to_thread(zeroconf.close)
        await runner.cleanup()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--duration-ms", type=int, default=8_000)
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--artifacts", type=Path, default=ROOT / "artifacts")
    parser.add_argument("--model", type=Path, default=ROOT / "models" / "ggml-small.en.bin")
    parser.add_argument("--phrase-audio", type=Path, default=ROOT / "artifacts" / "test-phrase.aiff")
    parser.add_argument("--speaker-volume", type=int, default=70)
    parser.add_argument("--debug-rtp", action="store_true")
    values = parser.parse_args()
    if values.runs < 1 or values.runs > 10:
        parser.error("--runs must be between 1 and 10")
    if values.speaker_volume < 1 or values.speaker_volume > 100:
        parser.error("--speaker-volume must be between 1 and 100")
    if not values.model.is_file():
        parser.error(f"Whisper model not found: {values.model}; run ./setup.sh")
    return values


if __name__ == "__main__":
    asyncio.run(main(parse_arguments()))
