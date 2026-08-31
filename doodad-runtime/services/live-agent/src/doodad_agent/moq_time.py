"""Optional plaintext listener serving only nonce-bound authenticated time.

No grant, token, key or protected resource is exposed on this listener. The
device must verify the MAC and a <=3 second round trip before trusting time.
HTTPS/WSS/media TLS validation is never disabled to solve cold-start time.
"""
import asyncio
import json
import time

from aiohttp import web

from .moq_auth import AuthorizationError, GrantRegistry
from .moq_ipc import _unique_object


class MoqTimeServer:
    def __init__(self, registry: GrantRegistry) -> None:
        self.registry = registry
        self._runner = None
        self._active = 0
        self._tokens, self._last = 32.0, time.monotonic()

    async def start(self, host: str, port: int) -> None:
        app = web.Application(client_max_size=1024)
        app.router.add_post('/v1/moq/time', self._time)
        runner = web.AppRunner(app, access_log=None, shutdown_timeout=2, keepalive_timeout=2)
        try:
            await runner.setup()
            await web.TCPSite(runner, host, port).start()
        except BaseException:
            await runner.cleanup()
            raise
        self._runner = runner

    async def close(self) -> None:
        if self._runner is not None:
            await self._runner.cleanup()
            self._runner = None

    async def _time(self, request: web.Request) -> web.Response:
        now = time.monotonic()
        self._tokens = min(32, self._tokens + (now-self._last)*16)
        self._last = now
        if self._active >= 8 or self._tokens < 1:
            raise web.HTTPTooManyRequests(text='time service busy')
        self._tokens -= 1
        self._active += 1
        try:
            if request.query_string or request.content_type != 'application/json':
                raise AuthorizationError()
            async with asyncio.timeout(2):
                body = json.loads(await request.read(), object_pairs_hook=_unique_object,
                                  parse_constant=lambda _: (_ for _ in ()).throw(AuthorizationError()))
            if not isinstance(body, dict) or set(body) != {'device_id', 'nonce'}:
                raise AuthorizationError()
            proof = self.registry.time_proof(body['device_id'], body['nonce'])
            return web.json_response(proof, headers={'Cache-Control': 'no-store'})
        except web.HTTPException:
            raise
        except Exception:
            raise web.HTTPForbidden(text='time authorization denied') from None
        finally:
            self._active -= 1
