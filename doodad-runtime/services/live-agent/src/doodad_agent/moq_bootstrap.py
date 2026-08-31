"""HTTPS-only bootstrap/WSS authorization boundary for the MoQ host.

The caller supplies the real session handler; there is no anonymous or WebRTC
fallback here. Device roots and trusted time must already be enrolled before
using HTTPS. A response's UTC value is not an unauthenticated time bootstrap.
"""
from __future__ import annotations

import asyncio
import json
import re
import ssl
from collections.abc import Awaitable, Callable

from aiohttp import web

from .moq_auth import AuthorizationError, GrantRegistry

ControlHandler = Callable[[web.WebSocketResponse, str, object], Awaitable[None]]


class MoqBootstrap:
    def __init__(self, registry: GrantRegistry, handler: ControlHandler,
                 *, media_host: str, media_port: int) -> None:
        if (not re.fullmatch(r"[a-zA-Z0-9.:-]{1,253}", media_host)
                or not 1 <= media_port <= 65535):
            raise ValueError("invalid MoQ endpoint address")
        self.registry, self.handler = registry, handler
        self.media_host, self.media_port = media_host, media_port
        self._runner: web.AppRunner | None = None
        self._sockets: set[web.WebSocketResponse] = set()
        self.unexpected_failures = 0

    async def start(self, host: str, port: int, context: ssl.SSLContext) -> None:
        if self._runner is not None:
            raise RuntimeError("bootstrap already started")
        if (context.protocol != ssl.PROTOCOL_TLS_SERVER
                or context.minimum_version < ssl.TLSVersion.TLSv1_2):
            raise ValueError("bootstrap requires a TLS server context with TLS 1.2 or newer")
        # aiohttp's default request logs include query strings. Disable them at
        # the serving boundary rather than relying on callers to redact later.
        runner = web.AppRunner(self.application(), access_log=None, shutdown_timeout=2)
        try:
            await runner.setup()
            await web.TCPSite(runner, host, port, ssl_context=context).start()
        except BaseException:
            await runner.cleanup()
            raise
        self._runner = runner

    async def close(self) -> None:
        if self._sockets:
            try:
                await asyncio.wait_for(asyncio.gather(
                    *(socket.close(code=1001) for socket in list(self._sockets)),
                    return_exceptions=True), 2)
            except TimeoutError:
                pass
        if self._runner is not None:
            await self._runner.cleanup()
            self._runner = None

    def application(self) -> web.Application:
        # Limits apply before JSON parsing. The runner MUST disable access logs:
        # it must never echo attacker-supplied query strings/headers on errors.
        app = web.Application(client_max_size=4096)
        app.router.add_post("/v1/moq/challenge", self.challenge)
        app.router.add_post("/v1/moq/bootstrap", self.bootstrap)
        app.router.add_get("/v1/moq/control", self.control)
        return app

    @staticmethod
    def _secure(request: web.Request) -> None:
        # Forwarded/X-Forwarded-Proto headers never confer TLS authentication.
        if not request.secure or request.query_string:
            raise web.HTTPForbidden(text="MoQ authorization denied")

    @staticmethod
    async def _body(request: web.Request, fields: set[str]) -> dict[str, str]:
        MoqBootstrap._secure(request)
        try:
            def unique(pairs):
                result = {}
                for key, value in pairs:
                    if key in result:
                        raise ValueError()
                    result[key] = value
                return result
            body = json.loads(await request.text(), object_pairs_hook=unique)
            if (not isinstance(body, dict) or set(body) != fields
                    or any(not isinstance(value, str) or len(value) > 128 for value in body.values())):
                raise ValueError()
            return body
        except (ValueError, UnicodeError, RecursionError):
            raise web.HTTPForbidden(text="MoQ authorization denied") from None

    async def challenge(self, request: web.Request) -> web.Response:
        body = await self._body(request, {"device_id"})
        try:
            nonce = self.registry.challenge(body["device_id"])
        except AuthorizationError:
            raise web.HTTPForbidden(text="MoQ authorization denied") from None
        return web.json_response({"challenge": nonce}, headers={"Cache-Control": "no-store"})

    async def bootstrap(self, request: web.Request) -> web.Response:
        body = await self._body(request, {"device_id", "challenge", "proof"})
        try:
            grant = self.registry.issue(body["device_id"], body["challenge"], body["proof"])
        except AuthorizationError:
            raise web.HTTPForbidden(text="MoQ authorization denied") from None
        return web.json_response({**grant.document(), "media_host": self.media_host,
                                  "media_port": self.media_port,
                                  "control_path": "/v1/moq/control", "transport": "moq-lite-05"},
                                 headers={"Cache-Control": "no-store"})

    async def control(self, request: web.Request) -> web.WebSocketResponse:
        self._secure(request)
        authorization = request.headers.get("Authorization", "")
        if not authorization.startswith("Bearer "):
            raise web.HTTPForbidden(text="MoQ authorization denied")
        owner = object()
        try:
            sid = self.registry.activate_control(authorization[7:], owner)
        except AuthorizationError:
            raise web.HTTPForbidden(text="MoQ authorization denied") from None
        socket = web.WebSocketResponse(max_msg_size=16 * 1024, heartbeat=15, compress=False)
        watch: asyncio.Task | None = None
        work: asyncio.Task | None = None
        try:
            await socket.prepare(request)
            self._sockets.add(socket)
            work = asyncio.create_task(self.handler(socket, sid, owner))
            watch = asyncio.create_task(self._watch_control(socket, sid, owner))
            done, _ = await asyncio.wait({work, watch}, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                await task
        except web.HTTPException:
            raise
        except Exception:
            # Application callbacks must not leak credentials/audio through a
            # default exception logger. Close/revoke and expose a safe counter.
            self.unexpected_failures += 1
        finally:
            self.registry.revoke(sid, owner)
            self._sockets.discard(socket)
            for task in (watch, work):
                if task is not None:
                    task.cancel()
            await asyncio.gather(*(t for t in (watch, work) if t is not None), return_exceptions=True)
            await socket.close()
        return socket

    async def _watch_control(self, socket: web.WebSocketResponse, sid: str, owner: object) -> None:
        while self.registry.valid(sid, owner) and not socket.closed:
            await asyncio.sleep(0.1)
        await socket.close(code=4003, message=b"MoQ session retired")
