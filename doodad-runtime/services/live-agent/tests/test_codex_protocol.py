from __future__ import annotations

import os
import threading
from pathlib import Path

import pytest

from doodad_agent.codex_protocol import AppServerClient, PINNED_CODEX_VERSION


def test_jsonl_reader_does_not_hide_bursted_turn_completion(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("DOODAD_PERSONAL_HMAC_KEY_HEX", "ab" * 32)
    monkeypatch.setenv("DOODAD_SMTP_PASSWORD", "host-only-email-secret")
    binary = tmp_path / "fake-codex"
    binary.write_text(
        f"""#!/usr/bin/env python3
import json
import os
import sys

if os.getenv('DOODAD_PERSONAL_HMAC_KEY_HEX'):
    raise SystemExit(9)
if os.getenv('DOODAD_SMTP_PASSWORD'):
    raise SystemExit(10)

if '--version' in sys.argv:
    print({PINNED_CODEX_VERSION!r})
    raise SystemExit(0)

for line in sys.stdin:
    message = json.loads(line)
    method = message.get('method')
    if method == 'initialize':
        print(json.dumps({{'id': message['id'], 'result': {{}}}}), flush=True)
    elif method == 'thread/start':
        print(json.dumps({{'id': message['id'], 'result': {{'thread': {{'id': 'thread-1'}}}}}}), flush=True)
    elif method == 'turn/start':
        if message['params'].get('collaborationMode', {{}}).get('mode') != 'plan':
            raise SystemExit(11)
        turn = {{'id': 'turn-1', 'status': 'inProgress', 'items': [], 'error': None}}
        completed = {{'id': 'turn-1', 'status': 'completed', 'items': [], 'error': None}}
        print(json.dumps({{'id': message['id'], 'result': {{'turn': turn}}}}))
        print(json.dumps({{'method': 'item/completed', 'params': {{'threadId': 'thread-1', 'turnId': 'turn-1', 'completedAtMs': 1, 'item': {{'type': 'agentMessage', 'id': 'message-1', 'phase': 'final_answer', 'text': '{{\"status\":\"ready\",\"summary\":\"done\"}}'}}}}}}))
        print(json.dumps({{'method': 'turn/completed', 'params': {{'threadId': 'thread-1', 'turn': completed}}}}), flush=True)
""",
        encoding="utf-8",
    )
    os.chmod(binary, 0o755)
    schemas = (
        Path(__file__).resolve().parents[1]
        / "src"
        / "doodad_agent"
        / "codex_protocol_schemas"
    )
    client = AppServerClient(binary, schemas, turn_timeout_seconds=5)
    started: list[tuple[str, str]] = []
    result = client.run_turn(
        workspace=tmp_path,
        prompt="test",
        thread_id=None,
        stop=threading.Event(),
        on_started=lambda thread_id, turn_id: started.append((thread_id, turn_id)),
        on_question=lambda _params: "ring",
        collaboration_mode="plan",
    )

    assert started == [("thread-1", "turn-1")]
    assert result.status == "completed"
    assert result.final_text == '{"status":"ready","summary":"done"}'


def test_active_turn_can_be_steered_and_interrupted(tmp_path: Path) -> None:
    binary = tmp_path / "fake-codex-control"
    binary.write_text(
        f"""#!/usr/bin/env python3
import json
import sys

if '--version' in sys.argv:
    print({PINNED_CODEX_VERSION!r})
    raise SystemExit(0)

for line in sys.stdin:
    message = json.loads(line)
    method = message.get('method')
    if method == 'initialize':
        print(json.dumps({{'id': message['id'], 'result': {{}}}}), flush=True)
    elif method == 'thread/start':
        print(json.dumps({{'id': message['id'], 'result': {{'thread': {{'id': 'thread-control'}}}}}}), flush=True)
    elif method == 'turn/start':
        turn = {{'id': 'turn-control', 'status': 'inProgress', 'items': [], 'error': None}}
        print(json.dumps({{'id': message['id'], 'result': {{'turn': turn}}}}), flush=True)
    elif method == 'turn/steer':
        print(json.dumps({{'id': message['id'], 'result': {{'turnId': 'turn-control'}}}}), flush=True)
    elif method == 'turn/interrupt':
        turn = {{'id': 'turn-control', 'status': 'interrupted', 'items': [], 'error': None}}
        print(json.dumps({{'id': message['id'], 'result': {{}}}}))
        print(json.dumps({{'method': 'turn/completed', 'params': {{'threadId': 'thread-control', 'turn': turn}}}}), flush=True)
""",
        encoding="utf-8",
    )
    os.chmod(binary, 0o755)
    schemas = (
        Path(__file__).resolve().parents[1]
        / "src"
        / "doodad_agent"
        / "codex_protocol_schemas"
    )
    client = AppServerClient(binary, schemas, turn_timeout_seconds=5)
    started = threading.Event()
    holder = []

    def run() -> None:
        holder.append(
            client.run_turn(
                workspace=tmp_path,
                prompt="test controls",
                thread_id=None,
                stop=threading.Event(),
                on_started=lambda _thread, _turn: started.set(),
                on_question=lambda _params: "ring",
            )
        )

    thread = threading.Thread(target=run)
    thread.start()
    assert started.wait(2)
    assert client.steer("focus on the manifest")
    assert client.interrupt()
    thread.join(2)

    assert not thread.is_alive()
    assert holder[0].status == "interrupted"
