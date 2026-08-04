#!/usr/bin/env python3
"""Local WebRTC receiver and physical voice-uplink conformance harness."""

from __future__ import annotations

import argparse
import asyncio
import array
import json
import logging
import re
import shutil
import socket
import subprocess
import sys
import time
import wave
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Any

import av
from aiohttp import WSMsgType, web
from aiortc import AudioStreamTrack, RTCPeerConnection, RTCSessionDescription
from av import AudioFrame
from av.audio.resampler import AudioResampler
from zeroconf import IPVersion, ServiceInfo, Zeroconf

from fidelity import (
    analyze_capture_wave,
    expand_inter_run_gaps,
    extract_wave_segment,
    make_tone_pcm,
    packet_timing_metrics,
    parse_playback_telemetry,
    write_wave_mono_s16,
)
from protocol import envelope, word_error_rate


ROOT = Path(__file__).resolve().parent
DEFAULT_PHRASE = "I'll set the timer for five minutes."
DEFAULT_DOWNLINK_PHRASE = (
    "Please set the timer for five minutes. Please set the timer for five minutes."
)
DEFAULT_TONE_HZ = 660.0
DEFAULT_TONE_DURATION_MS = 900
DEFAULT_TONE_AMPLITUDE = 16_000
DEFAULT_DOWNLINK_GAP_MS = 300
# Keep the bookend inside the firmware's 240 ms utterance drain window even
# when macOS `say` leaves a short low-level tail on the generated phrase.
DEFAULT_CLOSING_GAP_MS = 160
DEFAULT_CLOSING_TONE_HZ = 880.0
DEFAULT_CLOSING_TONE_DURATION_MS = 300
DEFAULT_RTP_WARMUP_MS = 100
DEFAULT_CAPTURE_DRAIN_SECONDS = 2.5
DOWNLINK_PCM_SAMPLE_RATE = 16_000
DOWNLINK_FRAME_SAMPLES = 320
DOWNLINK_CODEC_LABEL = "Opus"


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


def mac_input_volume() -> int | None:
    """Read, but never alter, the current macOS microphone input volume."""
    result = subprocess.run(
        [
            "osascript",
            "-e",
            "return input volume of (get volume settings) as string",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    try:
        return int(result.stdout.strip())
    except ValueError:
        return None


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


class DownlinkAudioTrack(AudioStreamTrack):
    """Paced 16 kHz mono source that aiortc encodes as wideband Opus."""

    def __init__(self) -> None:
        super().__init__()
        # Thirty seconds leaves room for a calibration tone, a gap, and a
        # spoken phrase without silently truncating the program.
        self._samples: asyncio.Queue[int] = asyncio.Queue(
            maxsize=DOWNLINK_PCM_SAMPLE_RATE * 30
        )
        self._rtp_timestamp = 0
        self._next_packet_deadline: float | None = None
        self._last_packet_time: float | None = None
        self.packet_times: list[float] = []
        self.frames_sent = 0
        self.audible_frames_sent = 0

    def enqueue_program(self, samples: array.array[int]) -> tuple[int, int]:
        """Enqueue one complete program and return its packet slice."""
        if self._samples.qsize() != 0:
            raise RuntimeError("a downlink program is already queued")
        if len(samples) > self._samples.maxsize:
            raise ValueError(
                f"downlink program has {len(samples)} samples; "
                f"maximum is {self._samples.maxsize}"
            )
        first_packet = len(self.packet_times)
        for sample in samples:
            self._samples.put_nowait(sample)
        packet_count = (
            len(samples) + DOWNLINK_FRAME_SAMPLES - 1
        ) // DOWNLINK_FRAME_SAMPLES
        return first_packet, first_packet + packet_count

    def enqueue_tone(
        self,
        frequency_hz: float = DEFAULT_TONE_HZ,
        duration_ms: int = DEFAULT_TONE_DURATION_MS,
        amplitude: int = DEFAULT_TONE_AMPLITUDE,
    ) -> tuple[int, int]:
        return self.enqueue_program(make_tone_pcm(
            frequency_hz,
            duration_ms,
            amplitude=amplitude,
            sample_rate=DOWNLINK_PCM_SAMPLE_RATE,
        ))

    async def wait_for_packets(self, end_packet: int, timeout: float) -> None:
        deadline = asyncio.get_running_loop().time() + timeout
        while len(self.packet_times) < end_packet:
            if asyncio.get_running_loop().time() >= deadline:
                raise TimeoutError(
                    f"sent {len(self.packet_times)} packets, waiting for {end_packet}"
                )
            await asyncio.sleep(0.01)

    async def recv(self):  # type: ignore[no-untyped-def]
        # Blocking here is intentional: there is no artificial RTP silence
        # between utterances, matching the production downlink track. The
        # firmware can therefore preserve real silent frames inside a program
        # and use packet absence as its end-of-utterance drain signal.
        first_sample = await self._samples.get()

        # Pace against a persistent deadline so time spent encoding/sending the
        # previous frame does not accumulate as drift. A delay of half a frame
        # is treated as an idle/scheduler gap and re-anchored, preventing the
        # catch-up bursts in aiortc's base AudioStreamTrack.
        loop = asyncio.get_running_loop()
        now = loop.time()
        deadline = self._next_packet_deadline
        reanchored = False
        if deadline is None:
            deadline = now
        else:
            delay = deadline - now
            if delay > 0:
                await asyncio.sleep(delay)
                now = loop.time()
            if now - deadline >= 0.010:
                deadline = now
                reanchored = True
        packet_time = loop.time()
        self._next_packet_deadline = deadline + 0.020

        frame = AudioFrame(
            format="s16", layout="mono", samples=DOWNLINK_FRAME_SAMPLES
        )
        frame.sample_rate = DOWNLINK_PCM_SAMPLE_RATE
        frame.pts = self._rtp_timestamp
        frame.time_base = Fraction(1, DOWNLINK_PCM_SAMPLE_RATE)
        if self._last_packet_time is not None and reanchored:
            elapsed_samples = round(
                (packet_time - self._last_packet_time) * DOWNLINK_PCM_SAMPLE_RATE
            )
            frame.pts = max(
                frame.pts,
                self._rtp_timestamp - DOWNLINK_FRAME_SAMPLES + elapsed_samples,
            )
        self._rtp_timestamp = frame.pts + DOWNLINK_FRAME_SAMPLES
        self._last_packet_time = packet_time
        pcm = array.array("h", [first_sample])
        audible = first_sample != 0
        for _ in range(frame.samples - 1):
            try:
                sample = self._samples.get_nowait()
                audible = audible or sample != 0
            except asyncio.QueueEmpty:
                sample = 0
            pcm.append(sample)
        frame.planes[0].update(pcm.tobytes())
        self.packet_times.append(packet_time)
        self.frames_sent += 1
        if audible:
            self.audible_frames_sent += 1
        return frame


@dataclass(frozen=True)
class DownlinkProgram:
    samples: array.array[int]
    tone_frequency_hz: float
    tone_duration_ms: int
    gap_duration_ms: int
    phrase_duration_ms: float
    closing_gap_duration_ms: int
    closing_tone_frequency_hz: float
    closing_tone_duration_ms: int

    @property
    def expected_duration_ms(self) -> float:
        return len(self.samples) * 1_000 / DOWNLINK_PCM_SAMPLE_RATE

    @property
    def wire_duration_ms(self) -> float:
        return (
            (len(self.samples) + DOWNLINK_FRAME_SAMPLES - 1)
            // DOWNLINK_FRAME_SAMPLES
        ) * 20.0

    @property
    def closing_tone_offset_ms(self) -> float:
        """Expected time from opening-tone onset to closing-tone onset."""
        return (
            self.tone_duration_ms
            + self.gap_duration_ms
            + self.phrase_duration_ms
            + self.closing_gap_duration_ms
        )


def decode_audio_mono_s16(
    path: Path, sample_rate: int = DOWNLINK_PCM_SAMPLE_RATE
) -> array.array[int]:
    """Decode an audio asset into packed mono PCM using the pinned PyAV."""
    result = array.array("h")
    resampler = AudioResampler(format="s16", layout="mono", rate=sample_rate)
    with av.open(str(path)) as container:
        for frame in container.decode(audio=0):
            for converted in resampler.resample(frame):
                payload = bytes(converted.planes[0])[:converted.samples * 2]
                result.frombytes(payload)
        for converted in resampler.resample(None):
            payload = bytes(converted.planes[0])[:converted.samples * 2]
            result.frombytes(payload)
    return result


def make_downlink_program(arguments: argparse.Namespace) -> DownlinkProgram:
    phrase = decode_audio_mono_s16(arguments.downlink_phrase_audio)
    samples = make_tone_pcm(
        arguments.downlink_tone_hz,
        arguments.downlink_tone_duration_ms,
        amplitude=DEFAULT_TONE_AMPLITUDE,
        sample_rate=DOWNLINK_PCM_SAMPLE_RATE,
    )
    samples.extend(array.array(
        "h", [0]) * (
            DOWNLINK_PCM_SAMPLE_RATE * arguments.downlink_gap_ms // 1_000
        )
    )
    samples.extend(phrase)
    samples.extend(array.array(
        "h", [0]) * (
            DOWNLINK_PCM_SAMPLE_RATE * DEFAULT_CLOSING_GAP_MS // 1_000
        )
    )
    samples.extend(make_tone_pcm(
        DEFAULT_CLOSING_TONE_HZ,
        DEFAULT_CLOSING_TONE_DURATION_MS,
        amplitude=DEFAULT_TONE_AMPLITUDE,
        sample_rate=DOWNLINK_PCM_SAMPLE_RATE,
    ))
    return DownlinkProgram(
        samples=samples,
        tone_frequency_hz=arguments.downlink_tone_hz,
        tone_duration_ms=arguments.downlink_tone_duration_ms,
        gap_duration_ms=arguments.downlink_gap_ms,
        phrase_duration_ms=len(phrase) * 1_000 / DOWNLINK_PCM_SAMPLE_RATE,
        closing_gap_duration_ms=DEFAULT_CLOSING_GAP_MS,
        closing_tone_frequency_hz=DEFAULT_CLOSING_TONE_HZ,
        closing_tone_duration_ms=DEFAULT_CLOSING_TONE_DURATION_MS,
    )


class FFmpegMicrophoneCapture:
    """Opt-in AVFoundation capture with permission-safe failure reporting."""

    def __init__(self, executable: str, device: str, sample_rate: int = 16_000) -> None:
        self.executable = executable
        self.device = device
        self.sample_rate = sample_rate
        self.process: asyncio.subprocess.Process | None = None
        self.log_path: Path | None = None

    async def start(self, output_path: Path, log_path: Path) -> None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path = log_path
        self.process = await asyncio.create_subprocess_exec(
            self.executable,
            "-hide_banner",
            "-loglevel", "warning",
            "-f", "avfoundation",
            "-i", self.device,
            "-ac", "1",
            "-ar", str(self.sample_rate),
            "-c:a", "pcm_s16le",
            "-y", str(output_path),
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.PIPE,
        )
        # AVFoundation opens asynchronously. Detect an invalid device or a
        # denied macOS microphone permission before sending audio to the watch.
        await asyncio.sleep(0.75)
        if self.process.returncode is not None:
            _, stderr = await self.process.communicate()
            self.process = None
            log_path.write_bytes(stderr)
            raise RuntimeError(
                "FFmpeg could not open the macOS microphone. Approve microphone "
                "access for Codex/Terminal in System Settings > Privacy & Security, "
                "or select another --capture-device. See " + str(log_path)
            )

    async def stop(self) -> None:
        if self.process is None:
            return
        process = self.process
        self.process = None
        if process.returncode is None and process.stdin is not None:
            process.stdin.write(b"q\n")
            await process.stdin.drain()
        try:
            _, stderr = await asyncio.wait_for(process.communicate(), timeout=5)
        except asyncio.TimeoutError:
            process.terminate()
            _, stderr = await process.communicate()
        if self.log_path is not None:
            self.log_path.write_bytes(stderr)
        if process.returncode not in (0, 255):
            raise RuntimeError(
                f"FFmpeg microphone capture exited with {process.returncode}; "
                f"see {self.log_path}"
            )


async def prepare_speech_for_transcription(
    executable: str,
    source: Path,
    destination: Path,
) -> None:
    """Band-limit and normalize a quiet room capture without changing timing."""
    process = await asyncio.create_subprocess_exec(
        executable,
        "-hide_banner",
        "-loglevel", "error",
        "-i", str(source),
        "-af", (
            "highpass=f=100,lowpass=f=3800,"
            "afftdn=nr=12:nf=-60:tn=1,"
            "loudnorm=I=-16:LRA=11:TP=-1.5"
        ),
        "-ar", "16000",
        "-ac", "1",
        "-y", str(destination),
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.PIPE,
    )
    _, stderr = await process.communicate()
    if process.returncode != 0:
        raise RuntimeError(
            f"FFmpeg speech analysis failed with {process.returncode}: "
            + stderr.decode("utf-8", errors="replace")
        )


class SerialTelemetryCapture:
    """Continuously retain firmware logs without blocking the asyncio loop."""

    def __init__(self, port: str, baud: int) -> None:
        self.port = port
        self.baud = baud
        self.lines: list[str] = []
        self.connection: Any = None
        self.task: asyncio.Task[None] | None = None
        self.writer: Any = None

    async def start(self, path: Path) -> None:
        import serial

        path.parent.mkdir(parents=True, exist_ok=True)
        self.writer = path.open("w", encoding="utf-8")
        connection = serial.Serial()
        connection.port = self.port
        connection.baudrate = self.baud
        connection.timeout = 0.1
        connection.dtr = False
        connection.rts = False
        await asyncio.to_thread(connection.open)
        self.connection = connection
        self.task = asyncio.create_task(self._consume())

    def mark(self) -> int:
        return len(self.lines)

    async def wait_for_playback(
        self, start: int, timeout: float
    ) -> tuple[dict[str, int] | None, str | None, list[str]]:
        deadline = asyncio.get_running_loop().time() + timeout
        checked = start
        while True:
            while checked < len(self.lines):
                line = self.lines[checked]
                checked += 1
                telemetry = parse_playback_telemetry(line)
                if telemetry is not None:
                    return telemetry, line, self.lines[start:checked]
            if asyncio.get_running_loop().time() >= deadline:
                return None, None, self.lines[start:]
            await asyncio.sleep(0.02)

    async def _consume(self) -> None:
        while self.connection is not None:
            payload = await asyncio.to_thread(self.connection.readline)
            if not payload:
                continue
            line = payload.decode("utf-8", errors="replace").rstrip("\r\n")
            self.lines.append(line)
            if self.writer is not None:
                self.writer.write(line + "\n")
                self.writer.flush()

    async def close(self) -> None:
        connection = self.connection
        self.connection = None
        if self.task is not None:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
            self.task = None
        if connection is not None:
            await asyncio.to_thread(connection.close)
        if self.writer is not None:
            self.writer.close()
            self.writer = None


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
    playback_analysis: str
    downlink_passed: bool
    inter_run_gap_s: float


class LabSession:
    def __init__(self, websocket: web.WebSocketResponse, arguments: argparse.Namespace) -> None:
        self.websocket = websocket
        self.arguments = arguments
        self.peer = RTCPeerConnection()
        self.sequence = 0
        self.messages: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.connected = asyncio.Event()
        self.recorder = SegmentRecorder()
        self.downlink = DownlinkAudioTrack()
        self.downlink_attached = False
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
            await self.send("welcome", {
                "mode": "duplex-audio",
                "audio": "opus-48000-rtp-16000-pcm-mono",
            })
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
        if "opus/48000" not in sdp.lower():
            raise RuntimeError("watch SDP offer did not advertise Opus/48000")
        (self.arguments.artifacts / "offer.sdp").write_text(sdp, encoding="utf-8")
        await self.peer.setRemoteDescription(
            RTCSessionDescription(sdp=sdp, type="offer")
        )
        if not self.downlink_attached:
            self.peer.addTrack(self.downlink)
            self.downlink_attached = True
        answer = await self.peer.createAnswer()
        await self.peer.setLocalDescription(answer)
        assert self.peer.localDescription is not None
        answer_sdp = keep_host_candidate(
            self.peer.localDescription.sdp, local_ipv4())
        if "opus/48000" not in answer_sdp.lower():
            raise RuntimeError("host SDP answer did not negotiate Opus/48000")
        (self.arguments.artifacts / "answer.sdp").write_text(
            answer_sdp, encoding="utf-8"
        )
        await self.send("sdp", {
            "kind": "answer",
            "sdp": answer_sdp,
        })
        print("signal -> sdp answer (Opus/48000 negotiated)", flush=True)

    async def wait_for(self, message_type: str, timeout: float) -> dict[str, Any]:
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            message = await asyncio.wait_for(self.messages.get(), remaining)
            if message.get("type") == message_type:
                return message
            if (
                message_type == "capture.stopped"
                and message.get("type") == "capture.failed"
            ):
                raise RuntimeError("watch reported capture.failed")

    async def run_downlink_fidelity(self, directory: Path) -> dict[str, Any]:
        program: DownlinkProgram = self.arguments.downlink_program
        source_path = directory / "speaker-downlink-source.wav"
        write_wave_mono_s16(
            source_path, DOWNLINK_PCM_SAMPLE_RATE, program.samples
        )
        capture_path = directory / "speaker-downlink-capture.wav"
        capture_log = directory / "ffmpeg-capture.log"
        capture: FFmpegMicrophoneCapture | None = None
        capture_input_volume: int | None = None
        serial_capture: SerialTelemetryCapture | None = self.arguments.serial_capture
        serial_start = serial_capture.mark() if serial_capture is not None else 0
        if self.arguments.capture_playback:
            capture_input_volume = mac_input_volume()
            capture = FFmpegMicrophoneCapture(
                self.arguments.ffmpeg,
                self.arguments.capture_device,
            )

        try:
            if capture is not None:
                await capture.start(capture_path, capture_log)
            # The ESP peer can discard the first couple of packets while its
            # receive jitter buffer transitions from negotiated to active.
            # Prime that path with silence which the firmware intentionally
            # ignores, so no calibration-tone or speech samples are sacrificed.
            warmup_samples = (
                DOWNLINK_PCM_SAMPLE_RATE * DEFAULT_RTP_WARMUP_MS // 1_000
            )
            wire_samples = array.array("h", [0]) * warmup_samples
            wire_samples.extend(program.samples)
            first_packet, end_packet = self.downlink.enqueue_program(wire_samples)
            first_packet += (
                warmup_samples + DOWNLINK_FRAME_SAMPLES - 1
            ) // DOWNLINK_FRAME_SAMPLES
            await self.downlink.wait_for_packets(
                end_packet,
                timeout=program.wire_duration_ms / 1_000 * 2 + 5,
            )
            # AVFoundation delivers microphone audio in large buffered chunks.
            # Leave enough room for both the watch's speaker queue and the
            # host capture pipeline to flush the closing marker into the WAV.
            await asyncio.sleep(DEFAULT_CAPTURE_DRAIN_SECONDS)
        finally:
            if capture is not None and capture.process is not None:
                await capture.stop()

        packet_times = self.downlink.packet_times[first_packet:end_packet]
        transport = packet_timing_metrics(
            packet_times,
            program.wire_duration_ms,
        )
        firmware: dict[str, Any] = {
            "enabled": False,
            "reason": "run with --serial-port to gate firmware playback counters",
        }
        if serial_capture is not None:
            telemetry, telemetry_line, serial_lines = (
                await serial_capture.wait_for_playback(serial_start, timeout=3.0)
            )
            serial_path = directory / "firmware-telemetry.log"
            serial_path.write_text(
                "\n".join(serial_lines) + ("\n" if serial_lines else ""),
                encoding="utf-8",
            )
            gates = {
                "opus_codec": telemetry is not None and telemetry.get("codec") == 3,
                "wideband_pcm": (
                    telemetry is not None and telemetry.get("pcm_rate") == 16_000
                ),
                "zero_dropped": telemetry is not None and telemetry["dropped"] == 0,
                "zero_underflow": telemetry is not None and telemetry["underflow"] == 0,
                "zero_speaker_fail": (
                    telemetry is not None and telemetry["speaker_fail"] == 0
                ),
            }
            firmware = {
                "enabled": True,
                "port": serial_capture.port,
                "log": str(serial_path),
                "final_line": telemetry_line,
                "counters": telemetry,
                "gates": gates,
                "passed": all(gates.values()),
            }
        analysis: dict[str, Any] = {
            "schema_version": 2,
            "phrase": self.arguments.downlink_phrase,
            "uplink_phrase": self.arguments.phrase,
            "downlink_phrase": self.arguments.downlink_phrase,
            "program": {
                "codec": "opus",
                "sample_rate": DOWNLINK_PCM_SAMPLE_RATE,
                "samples": len(program.samples),
                "tone_frequency_hz": program.tone_frequency_hz,
                "tone_duration_ms": program.tone_duration_ms,
                "gap_duration_ms": program.gap_duration_ms,
                "phrase_duration_ms": round(program.phrase_duration_ms, 3),
                "closing_gap_duration_ms": program.closing_gap_duration_ms,
                "closing_tone_frequency_hz": program.closing_tone_frequency_hz,
                "closing_tone_duration_ms": program.closing_tone_duration_ms,
                "closing_tone_offset_ms": round(
                    program.closing_tone_offset_ms, 3
                ),
                "expected_duration_ms": round(program.expected_duration_ms, 3),
                "wire_duration_ms": round(program.wire_duration_ms, 3),
                "rtp_warmup_ms": DEFAULT_RTP_WARMUP_MS,
                "source_wav": str(source_path),
            },
            "transport": transport,
            "firmware": firmware,
            "capture": {
                "enabled": False,
                "reason": "run with --capture-playback to record the CoreS3 speaker",
            },
        }

        if capture is not None:
            physical = analyze_capture_wave(
                capture_path,
                expected_tone_hz=program.tone_frequency_hz,
                expected_tone_duration_ms=program.tone_duration_ms,
                expected_closing_tone_hz=program.closing_tone_frequency_hz,
                expected_closing_tone_duration_ms=program.closing_tone_duration_ms,
                expected_marker_offset_ms=program.closing_tone_offset_ms,
                expected_program_duration_ms=program.expected_duration_ms,
            )
            physical["enabled"] = True
            physical["wav"] = str(capture_path)
            physical["device"] = self.arguments.capture_device
            physical["mac_input_volume"] = capture_input_volume

            tone = physical.get("tone") or {}
            closing_tone = physical.get("closing_tone") or {}
            tone_start_ms = tone.get("start_ms")
            closing_start_ms = closing_tone.get("start_ms")
            if (
                isinstance(tone_start_ms, (int, float))
                and isinstance(closing_start_ms, (int, float))
                and closing_start_ms > tone_start_ms
            ):
                speech_path = directory / "speaker-downlink-speech.wav"
                observed_marker_offset_ms = (
                    float(closing_start_ms) - float(tone_start_ms)
                )
                clock_scale = (
                    observed_marker_offset_ms / program.closing_tone_offset_ms
                )
                speech_start_ms = max(0.0, float(tone_start_ms) + clock_scale * (
                    program.tone_duration_ms + program.gap_duration_ms
                ))
                speech_end_ms = float(closing_start_ms) - (
                    clock_scale * program.closing_gap_duration_ms
                )
                extract_wave_segment(
                    capture_path,
                    speech_path,
                    start_ms=speech_start_ms,
                    duration_ms=max(0.0, speech_end_ms - speech_start_ms),
                )
                speech_analysis_path = (
                    directory / "speaker-downlink-speech-analysis.wav"
                )
                await prepare_speech_for_transcription(
                    self.arguments.ffmpeg,
                    speech_path,
                    speech_analysis_path,
                )
                playback_transcript = await transcribe(
                    speech_analysis_path,
                    self.arguments.model,
                    directory,
                    output_name="speaker-whisper",
                )
                playback_wer = word_error_rate(
                    self.arguments.downlink_phrase, playback_transcript
                )
                physical["speech_wav"] = str(speech_path)
                physical["speech_analysis_wav"] = str(speech_analysis_path)
                physical["transcript"] = playback_transcript
                physical["wer"] = round(playback_wer, 6)
                physical["speech_start_ms"] = round(speech_start_ms, 3)
                physical["speech_end_ms"] = round(speech_end_ms, 3)
                physical["speech_clock_scale"] = round(clock_scale, 6)
                physical["gates"]["speech_wer"] = playback_wer <= 0.25
            else:
                physical["transcript"] = None
                physical["wer"] = None
                physical["gates"]["speech_wer"] = False
            physical["passed"] = all(physical["gates"].values())
            analysis["capture"] = physical

        analysis["passed"] = (
            bool(transport["passed"])
            and (
                serial_capture is None
                or bool(analysis["firmware"].get("passed"))
            )
            and (
                not self.arguments.capture_playback
                or bool(analysis["capture"].get("passed"))
            )
        )
        analysis_path = directory / "playback-analysis.json"
        analysis_path.write_text(
            json.dumps(analysis, indent=2) + "\n", encoding="utf-8"
        )
        capture_label = (
            f", physical capture passed={analysis['capture'].get('passed')}"
            if self.arguments.capture_playback
            else ", physical capture skipped"
        )
        print(
            f"downlink sent {transport['packet_count']} paced "
            f"{DOWNLINK_CODEC_LABEL}-source frames; "
            f"min interval={transport['min_packet_interval_ms']} ms, "
            f"transport passed={transport['passed']}{capture_label}",
            flush=True,
        )
        return analysis

    async def run_lab(self) -> None:
        await asyncio.wait_for(self.connected.wait(), 30)
        await asyncio.sleep(0.5)
        for run_number in range(1, self.arguments.runs + 1):
            inter_run_gap = self.arguments.inter_run_gaps[run_number - 1]
            if inter_run_gap > 0:
                print(
                    f"run {run_number}: waiting {inter_run_gap:g} s idle-gap probe",
                    flush=True,
                )
                await asyncio.sleep(inter_run_gap)
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
            playback = await self.run_downlink_fidelity(directory)
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
                playback_analysis=str(directory / "playback-analysis.json"),
                downlink_passed=bool(playback["passed"]),
                inter_run_gap_s=inter_run_gap,
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
            "downlink_phrase": self.arguments.downlink_phrase,
            "inter_run_gaps_s": self.arguments.inter_run_gaps,
            "runs": [result.__dict__ for result in self.results],
            "mean_wer": sum(result.wer for result in self.results) / len(self.results),
            "passed": all(
                result.encoded_frames > 0
                and result.received_frames > 0
                and result.downlink_passed
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


async def transcribe(
    wav_path: Path,
    model: Path,
    directory: Path,
    output_name: str = "whisper",
) -> str:
    output_base = directory / output_name
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
    (directory / f"{output_name}.log").write_bytes(output)
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
                            lambda task: report_runner_result(
                                task, state["done"], websocket
                            )
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


def report_runner_result(
    task: asyncio.Task[None],
    done: asyncio.Event,
    websocket: web.WebSocketResponse,
) -> None:
    if task.cancelled():
        done.set()
        return
    error = task.exception()
    if error is not None:
        print(f"lab failed: {error}", flush=True)
        asyncio.create_task(
            websocket.close(code=1011, message=b"lab failed")
        )
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
    make_phrase_audio(
        arguments.downlink_phrase,
        arguments.downlink_phrase_audio,
    )
    arguments.downlink_program = make_downlink_program(arguments)
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
    serial_capture: SerialTelemetryCapture | None = None
    if arguments.serial_port:
        serial_capture = SerialTelemetryCapture(
            arguments.serial_port, arguments.serial_baud
        )
        await serial_capture.start(arguments.artifacts / "firmware-telemetry.log")
        print(
            f"firmware telemetry: {arguments.serial_port} at {arguments.serial_baud} baud",
            flush=True,
        )
    arguments.serial_capture = serial_capture
    ip = local_ipv4()
    zeroconf, service = await asyncio.to_thread(
        advertise, ip, arguments.port)
    print(f"Echo Bridge listening at ws://{ip}:{arguments.port}/ws", flush=True)
    print(f"uplink phrase: {arguments.phrase!r}", flush=True)
    print(f"downlink phrase: {arguments.downlink_phrase!r}", flush=True)
    print(
        "downlink program: "
        f"{arguments.downlink_tone_hz:g} Hz tone + "
        f"{arguments.downlink_gap_ms} ms gap + spoken phrase + "
        f"{DEFAULT_CLOSING_GAP_MS} ms gap + "
        f"{DEFAULT_CLOSING_TONE_HZ:g} Hz closing marker "
        f"({arguments.downlink_program.expected_duration_ms:.0f} ms total)",
        flush=True,
    )
    if arguments.capture_playback:
        print(
            f"physical speaker capture enabled via {arguments.capture_device!r}",
            flush=True,
        )
    try:
        await done.wait()
    finally:
        await asyncio.to_thread(zeroconf.unregister_service, service)
        await asyncio.to_thread(zeroconf.close)
        await runner.cleanup()
        if serial_capture is not None:
            await serial_capture.close()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument(
        "--inter-run-gaps",
        default="0",
        help="one delay or one comma-separated pre-run delay per run (for example 0,2,10)",
    )
    parser.add_argument("--duration-ms", type=int, default=8_000)
    parser.add_argument("--phrase", default=DEFAULT_PHRASE)
    parser.add_argument("--artifacts", type=Path, default=ROOT / "artifacts")
    parser.add_argument("--model", type=Path, default=ROOT / "models" / "ggml-small.en.bin")
    parser.add_argument("--phrase-audio", type=Path, default=ROOT / "artifacts" / "test-phrase.aiff")
    parser.add_argument("--downlink-phrase", default=DEFAULT_DOWNLINK_PHRASE)
    parser.add_argument(
        "--downlink-phrase-audio",
        type=Path,
        default=ROOT / "artifacts" / "downlink-phrase.aiff",
    )
    parser.add_argument("--speaker-volume", type=int, default=70)
    parser.add_argument(
        "--capture-playback",
        action="store_true",
        help="record the CoreS3 speaker with FFmpeg and the macOS default microphone",
    )
    parser.add_argument(
        "--capture-device",
        default=":default",
        help="FFmpeg AVFoundation input (default: :default; examples: :0 or ':MacBook Pro Microphone')",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument(
        "--serial-port",
        help="optional firmware serial port used to gate playback counters",
    )
    parser.add_argument("--serial-baud", type=int, default=115_200)
    parser.add_argument("--downlink-tone-hz", type=float, default=DEFAULT_TONE_HZ)
    parser.add_argument(
        "--downlink-tone-duration-ms",
        type=int,
        default=DEFAULT_TONE_DURATION_MS,
    )
    parser.add_argument(
        "--downlink-gap-ms",
        type=int,
        default=DEFAULT_DOWNLINK_GAP_MS,
    )
    parser.add_argument("--debug-rtp", action="store_true")
    values = parser.parse_args()
    if values.runs < 1 or values.runs > 10:
        parser.error("--runs must be between 1 and 10")
    try:
        values.inter_run_gaps = expand_inter_run_gaps(
            values.inter_run_gaps, values.runs
        )
    except ValueError as error:
        parser.error(str(error))
    if values.speaker_volume < 1 or values.speaker_volume > 100:
        parser.error("--speaker-volume must be between 1 and 100")
    if values.downlink_tone_hz < 100 or values.downlink_tone_hz > 3_000:
        parser.error("--downlink-tone-hz must be between 100 and 3000")
    if values.downlink_tone_duration_ms < 200 or values.downlink_tone_duration_ms > 5_000:
        parser.error("--downlink-tone-duration-ms must be between 200 and 5000")
    if values.downlink_gap_ms < 0 or values.downlink_gap_ms > 2_000:
        parser.error("--downlink-gap-ms must be between 0 and 2000")
    if not values.model.is_file():
        parser.error(f"Whisper model not found: {values.model}; run ./setup.sh")
    if values.capture_playback:
        if sys.platform != "darwin":
            parser.error("--capture-playback currently requires macOS AVFoundation")
        executable = shutil.which(values.ffmpeg)
        if executable is None:
            parser.error(f"FFmpeg not found: {values.ffmpeg}")
        values.ffmpeg = executable
    if values.serial_port:
        try:
            import serial  # noqa: F401
        except ImportError:
            parser.error("pyserial is not installed; run ./tools/voice-uplink/setup.sh")
        if values.serial_baud < 1:
            parser.error("--serial-baud must be positive")
    return values


if __name__ == "__main__":
    asyncio.run(main(parse_arguments()))
