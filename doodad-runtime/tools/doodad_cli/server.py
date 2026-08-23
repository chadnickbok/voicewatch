from __future__ import annotations

import json
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from socketserver import TCPServer
from typing import Any


class PreviewState:
    def __init__(self, frame: Path):
        self.frame = frame
        self.value: dict[str, Any] = {
            "revision": 0,
            "status": "starting",
            "message": "Building app",
            "stale": False,
            "surface": {
                "width": 410,
                "height": 502,
                "profile": "twatch_ultra_410x502",
            },
            "engine": "WAMR 2.4.0",
            "renderer": "LVGL 9.5.0",
        }
        self.lock = threading.Lock()

    def update(self, **values: Any) -> None:
        with self.lock:
            self.value.update(values)

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return dict(self.value)


class PreviewServer(ThreadingHTTPServer):
    """HTTP server that never blocks startup on a reverse-DNS lookup."""

    def server_bind(self) -> None:
        TCPServer.server_bind(self)
        self.server_name = str(self.server_address[0])
        self.server_port = int(self.server_address[1])


def make_handler(web_root: Path, state: PreviewState) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            route = self.path.split("?", 1)[0]
            if route == "/state.json":
                payload = json.dumps(state.snapshot()).encode()
                self._send("application/json", payload, cache=False)
                return
            if route == "/frame.bmp":
                try:
                    payload = state.frame.read_bytes()
                except FileNotFoundError:
                    self.send_error(HTTPStatus.NOT_FOUND)
                    return
                self._send("image/bmp", payload, cache=False)
                return
            files = {
                "/": ("index.html", "text/html; charset=utf-8"),
                "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                "/style.css": ("style.css", "text/css; charset=utf-8"),
            }
            selected = files.get(route)
            if selected is None:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            filename, content_type = selected
            self._send(content_type, (web_root / filename).read_bytes(), cache=True)

        def _send(self, content_type: str, payload: bytes, cache: bool) -> None:
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header(
                "Cache-Control", "public, max-age=300" if cache else "no-store"
            )
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *_: object) -> None:
            return

    return Handler


def start_server(
    web_root: Path, state: PreviewState, port: int
) -> tuple[ThreadingHTTPServer, threading.Thread]:
    server = PreviewServer(("127.0.0.1", port), make_handler(web_root, state))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread
