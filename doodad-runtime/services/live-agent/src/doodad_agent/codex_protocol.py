"""Supervised JSONL client for the pinned Codex app-server protocol."""

from __future__ import annotations

import itertools
import json
import os
import queue
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from jsonschema import Draft7Validator


PINNED_CODEX_VERSION = "codex-cli 0.146.0-alpha.9.2"
_OUTER_PACKAGER_SECRET = "DOODAD_PERSONAL_HMAC_KEY_HEX"


def _codex_environment() -> dict[str, str]:
    """Keep the outer packaging secret out of the untrusted builder process."""

    environment = os.environ.copy()
    environment.pop(_OUTER_PACKAGER_SECRET, None)
    return environment


class CodexProtocolError(RuntimeError):
    pass


@dataclass(frozen=True)
class CodexTurnResult:
    thread_id: str
    turn_id: str
    status: str
    final_text: str
    error: str | None = None


StartedCallback = Callable[[str, str], None]
QuestionCallback = Callable[[dict[str, Any]], str | None]


class AppServerClient:
    """Own one app-server process for one turn and suppress raw stream data."""

    def __init__(
        self,
        binary: Path | str,
        schema_directory: Path,
        *,
        expected_version: str = PINNED_CODEX_VERSION,
        turn_timeout_seconds: int = 1_800,
    ) -> None:
        self.binary = str(binary)
        self.schema_directory = schema_directory
        self.expected_version = expected_version
        self.turn_timeout_seconds = turn_timeout_seconds
        self._process: subprocess.Popen[str] | None = None
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._control_ids = itertools.count(100)
        self._active_thread_id: str | None = None
        self._active_turn_id: str | None = None

    def check_version(self) -> str:
        result = subprocess.run(
            [self.binary, "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
            env=_codex_environment(),
        )
        version = result.stdout.strip()
        if result.returncode != 0 or version != self.expected_version:
            raise CodexProtocolError(
                f"Codex version mismatch: expected {self.expected_version!r}, got {version!r}"
            )
        return version

    def close(self) -> None:
        try:
            self.interrupt()
        except (CodexProtocolError, OSError):
            pass
        with self._lock:
            process = self._process
        if process is not None and process.poll() is None:
            process.terminate()

    def steer(self, text: str) -> bool:
        with self._lock:
            process = self._process
            thread_id = self._active_thread_id
            turn_id = self._active_turn_id
        if process is None or thread_id is None or turn_id is None:
            return False
        params = {
            "threadId": thread_id,
            "expectedTurnId": turn_id,
            "input": [{"type": "text", "text": text}],
        }
        self._validate("v2/TurnSteerParams.json", params)
        self._send(
            process,
            {"method": "turn/steer", "id": next(self._control_ids), "params": params},
        )
        return True

    def interrupt(self) -> bool:
        with self._lock:
            process = self._process
            thread_id = self._active_thread_id
            turn_id = self._active_turn_id
        if process is None or thread_id is None or turn_id is None:
            return False
        params = {"threadId": thread_id, "turnId": turn_id}
        self._validate("v2/TurnInterruptParams.json", params)
        self._send(
            process,
            {
                "method": "turn/interrupt",
                "id": next(self._control_ids),
                "params": params,
            },
        )
        return True

    def run_turn(
        self,
        *,
        workspace: Path,
        prompt: str,
        thread_id: str | None,
        stop: threading.Event,
        on_started: StartedCallback,
        on_question: QuestionCallback,
    ) -> CodexTurnResult:
        self.check_version()
        process = subprocess.Popen(
            [self.binary, "app-server", "--stdio", "--strict-config"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
            env=_codex_environment(),
        )
        with self._lock:
            self._process = process
        try:
            return self._drive(
                process, workspace, prompt, thread_id, stop, on_started, on_question
            )
        finally:
            with self._lock:
                self._process = None
                self._active_thread_id = None
                self._active_turn_id = None
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)

    def _drive(
        self,
        process: subprocess.Popen[str],
        workspace: Path,
        prompt: str,
        thread_id: str | None,
        stop: threading.Event,
        on_started: StartedCallback,
        on_question: QuestionCallback,
    ) -> CodexTurnResult:
        if process.stdin is None or process.stdout is None:
            raise CodexProtocolError("app-server stdio pipes were not created")
        messages: queue.Queue[str | None] = queue.Queue()

        def read_stdout() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                messages.put(line)
            messages.put(None)

        threading.Thread(
            target=read_stdout,
            name="doodad-codex-stdout",
            daemon=True,
        ).start()
        deadline = time.monotonic() + self.turn_timeout_seconds

        initialize = {
            "clientInfo": {
                "name": "doodad_live_agent",
                "title": "Doodad Live Agent",
                "version": "0.1.0",
            },
            "capabilities": {
                "experimentalApi": True,
                "optOutNotificationMethods": [
                    "item/agentMessage/delta",
                    "item/reasoning/summaryTextDelta",
                    "item/reasoning/summaryPartAdded",
                    "item/reasoning/textDelta",
                    "item/commandExecution/outputDelta",
                    "turn/diff/updated",
                    "turn/plan/updated",
                ],
            },
        }
        self._validate("v1/InitializeParams.json", initialize)
        self._send(process, {"method": "initialize", "id": 1, "params": initialize})
        self._wait_response(process, messages, 1, stop, deadline, on_question)
        self._send(process, {"method": "initialized", "params": {}})

        if thread_id is None:
            thread_params: dict[str, Any] = {
                "cwd": str(workspace),
                "approvalPolicy": "never",
                "sandbox": "workspace-write",
                "serviceName": "doodad_live_agent",
            }
            self._validate("v2/ThreadStartParams.json", thread_params)
            method = "thread/start"
        else:
            thread_params = {"threadId": thread_id, "cwd": str(workspace)}
            self._validate("v2/ThreadResumeParams.json", thread_params)
            method = "thread/resume"
        self._send(process, {"method": method, "id": 2, "params": thread_params})
        thread_response = self._wait_response(
            process, messages, 2, stop, deadline, on_question
        )
        active_thread = str(thread_response["result"]["thread"]["id"])

        output_schema = {
            "type": "object",
            "additionalProperties": False,
            "required": ["status", "summary"],
            "properties": {
                "status": {
                    "type": "string",
                    "enum": ["ready", "needs_input", "failed"],
                },
                "summary": {"type": "string", "maxLength": 240},
            },
        }
        turn_params = {
            "threadId": active_thread,
            "input": [{"type": "text", "text": prompt}],
            "cwd": str(workspace),
            "approvalPolicy": "never",
            "sandboxPolicy": {
                "type": "workspaceWrite",
                "writableRoots": [str(workspace)],
                "networkAccess": False,
            },
            "summary": "concise",
            "effort": "medium",
            "outputSchema": output_schema,
        }
        self._validate("v2/TurnStartParams.json", turn_params)
        self._send(process, {"method": "turn/start", "id": 3, "params": turn_params})
        turn_response = self._wait_response(
            process, messages, 3, stop, deadline, on_question
        )
        turn_id = str(turn_response["result"]["turn"]["id"])
        with self._lock:
            self._active_thread_id = active_thread
            self._active_turn_id = turn_id
        on_started(active_thread, turn_id)

        final_text = ""
        while True:
            message = self._read(process, messages, stop, deadline)
            if "id" in message and "method" in message:
                self._answer_server_request(process, message, on_question)
                continue
            method = message.get("method")
            params = message.get("params", {})
            if method == "item/completed":
                item = params.get("item", {})
                if item.get("type") == "agentMessage" and item.get("phase") != "commentary":
                    final_text = str(item.get("text", ""))
            elif method == "turn/completed" and str(
                params.get("turn", {}).get("id")
            ) == turn_id:
                turn = params["turn"]
                error = turn.get("error")
                error_text = None
                if isinstance(error, dict):
                    error_text = str(error.get("message", "Codex turn failed"))[:240]
                return CodexTurnResult(
                    active_thread,
                    turn_id,
                    str(turn.get("status", "failed")),
                    final_text,
                    error_text,
                )

    def _wait_response(
        self,
        process: subprocess.Popen[str],
        messages: queue.Queue[str | None],
        request_id: int,
        stop: threading.Event,
        deadline: float,
        on_question: QuestionCallback,
    ) -> dict[str, Any]:
        while True:
            message = self._read(process, messages, stop, deadline)
            if message.get("id") == request_id and "method" not in message:
                if "error" in message:
                    raise CodexProtocolError(str(message["error"]))
                return message
            if "id" in message and "method" in message:
                self._answer_server_request(process, message, on_question)

    def _read(
        self,
        process: subprocess.Popen[str],
        messages: queue.Queue[str | None],
        stop: threading.Event,
        deadline: float,
    ) -> dict[str, Any]:
        while True:
            if stop.is_set():
                raise CodexProtocolError("worker stopped")
            if time.monotonic() >= deadline:
                raise CodexProtocolError("Codex turn timed out")
            try:
                line = messages.get(timeout=0.25)
            except queue.Empty:
                if process.poll() is not None:
                    raise CodexProtocolError(
                        f"app-server exited before turn completion ({process.returncode})"
                    )
                continue
            if line is None:
                raise CodexProtocolError(
                    f"app-server closed stdout before turn completion ({process.poll()})"
                )
            try:
                message = json.loads(line)
            except json.JSONDecodeError as error:
                raise CodexProtocolError("app-server emitted invalid JSON") from error
            if not isinstance(message, dict):
                raise CodexProtocolError("app-server message was not an object")
            return message

    def _answer_server_request(
        self,
        process: subprocess.Popen[str],
        message: dict[str, Any],
        on_question: QuestionCallback,
    ) -> None:
        method = str(message.get("method", ""))
        request_id = message["id"]
        if method == "item/tool/requestUserInput":
            params = message.get("params", {})
            self._validate("ToolRequestUserInputParams.json", params)
            answer = on_question(params)
            if answer is None:
                raise CodexProtocolError("worker stopped while awaiting user input")
            answers = {
                str(question["id"]): {"answers": [answer]}
                for question in params.get("questions", [])
            }
            result = {"answers": answers}
            self._validate("ToolRequestUserInputResponse.json", result)
            self._send(process, {"id": request_id, "result": result})
            return
        if method == "item/commandExecution/requestApproval":
            self._send(process, {"id": request_id, "result": {"decision": "decline"}})
            return
        if method == "item/fileChange/requestApproval":
            self._send(process, {"id": request_id, "result": {"decision": "decline"}})
            return
        raise CodexProtocolError(f"unsupported app-server request: {method}")

    def _send(self, process: subprocess.Popen[str], message: dict[str, Any]) -> None:
        if process.stdin is None:
            raise CodexProtocolError("app-server stdin is closed")
        with self._write_lock:
            process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
            process.stdin.flush()

    def _validate(self, relative_path: str, value: object) -> None:
        path = self.schema_directory / relative_path
        if not path.is_file():
            raise CodexProtocolError(f"missing generated protocol schema: {path}")
        schema = json.loads(path.read_text(encoding="utf-8"))
        errors = sorted(
            Draft7Validator(schema).iter_errors(value),
            key=lambda item: "/".join(map(str, item.path)),
        )
        if errors:
            raise CodexProtocolError(
                f"protocol payload failed {relative_path}: {errors[0].message}"
            )
