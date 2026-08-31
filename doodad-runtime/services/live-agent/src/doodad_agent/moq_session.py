"""Authenticated product session over WSS control and the native MoQ worker.

No callback writes IPC directly. A bounded writer owns both output channels;
one paced response pump may have one PCM packet awaiting that writer. Native
capture completion and watch speaker completion are separate, checked events.
Errors deliberately contain no untrusted documents, credentials or audio.
"""
from __future__ import annotations

import asyncio
import json
import traceback
from contextvars import ContextVar
from dataclasses import dataclass, field
from typing import Any
from collections.abc import Callable

from aiohttp import WSMsgType

from .audio import PacingOverrun, PcmSpool
from .moq_auth import GrantRegistry
from .moq_bridge import BridgePeer
from .moq_ipc import Packet, _unique_object
from .session import ACTION_CURRENT, ControlSession

_INTENT: ContextVar[tuple[object, int] | None] = ContextVar('moq_application_intent', default=None)


class MoqSessionError(ConnectionError):
    def __init__(self) -> None:
        super().__init__("MoQ session contract failed")


def decimal(value: object, *, maximum: int = 2**64 - 1) -> int:
    if (not isinstance(value, str) or not 1 <= len(value) <= 20
            or not value.isascii() or not value.isdecimal()
            or (len(value) > 1 and value[0] == '0')):
        raise MoqSessionError()
    result = int(value)
    if result > maximum:
        raise MoqSessionError()
    return result


@dataclass(frozen=True)
class Identity:
    capture_id: str
    request_id: str
    owner_token: str

    @classmethod
    def parse(cls, payload: dict) -> Identity:
        values = [payload.get(k) for k in ('capture_id', 'request_id', 'owner_token')]
        if not decimal(values[0]):
            raise MoqSessionError()
        decimal(values[1])
        decimal(values[2])
        return cls(*values)

    def fields(self) -> dict[str, str]:
        return dict(capture_id=self.capture_id, request_id=self.request_id,
                    owner_token=self.owner_token)


@dataclass(repr=False)
class Capture:
    identity: Identity
    first: int
    deadline: float
    start_id: int = 0
    end: int | None = None
    samples: int | None = None
    received: int = 0
    validated: asyncio.Event = field(default_factory=asyncio.Event)
    stop_requested: bool = False
    stopped_payload: dict = field(default_factory=dict)


@dataclass(repr=False)
class ResponseContext:
    identity: Identity
    request_id: int
    kind: str
    ready: asyncio.Event = field(default_factory=asyncio.Event)


class ResponseContextBusy(ConnectionError):
    pass


@dataclass(repr=False)
class Response:
    context: Capture | ResponseContext
    number: int
    generation: int
    first: int | None = None
    end: int | None = None
    samples: int = 0
    end_queued: bool = False
    end_sent: bool = False
    cancelled: bool = False
    prepared: asyncio.Event = field(default_factory=asyncio.Event)
    bound: asyncio.Event = field(default_factory=asyncio.Event)
    encoded: asyncio.Event = field(default_factory=asyncio.Event)
    finished: asyncio.Event = field(default_factory=asyncio.Event)
    done: asyncio.Event = field(default_factory=asyncio.Event)
    task: asyncio.Task | None = None

    def fields(self) -> dict[str, str]:
        return {**self.context.identity.fields(), 'response_id': str(self.number)}


@dataclass(repr=False)
class Outbound:
    channel: str
    kind: str
    payload: dict
    pcm: bytes = b''
    response: Response | None = None
    sent: asyncio.Future | None = None
    is_current: Callable[[], bool] | None = None


class MoqSession(ControlSession):
    explicit_capture_completion = True

    def __init__(self, websocket, trace, on_audio, on_event, on_identified,
                 *, registry: GrantRegistry, session_id: str, owner: object,
                 max_spool_seconds: int = 600) -> None:
        super().__init__(websocket, trace, on_audio, on_event, on_identified)
        self.registry, self.session_id, self.owner = registry, session_id, owner
        self.device_id = registry.identity(session_id, owner)
        if not 1 <= max_spool_seconds <= 600:
            raise ValueError('MoQ response spool must be between 1 and 600 seconds')
        self.downlink = PcmSpool(trace, max_spool_seconds=max_spool_seconds, continuous_timeline=True)
        self.peer: BridgePeer | None = None
        self._hello = self._identified = self._native_ready = self._watch_ready = False
        self._received = self._highest_capture = self._highest_response = 0
        self._highest_start = self._retired_start = self._pending_start = 0
        self._pending_stop = False
        self._start_deadline = 0.0
        self._intent = 0
        self._capture: Capture | None = None
        self._response_context: ResponseContext | None = None
        self._highest_context_request = 0
        self._pending_context: tuple[int, str, asyncio.Future] | None = None
        self._response: Response | None = None
        self._out: asyncio.Queue[Outbound] = asyncio.Queue(32)
        self._app: asyncio.Queue[tuple[str, Any, Capture | None, int | None]] = asyncio.Queue(64)
        self._tasks: set[asyncio.Task] = set()
        self._fault = asyncio.Event()
        self._started = False
        self._startup_deadline = asyncio.get_running_loop().time() + 30
        self._renewal_supported = False
        self._renewal_pending: dict | None = None
        self._renewal_native_ack = False
        self._renewal_deadline = 0.0
        self.renewals_completed = 0

    def _check(self) -> None:
        if self._closed or self._fault.is_set() or not self.registry.valid(self.session_id, self.owner):
            raise MoqSessionError()

    def _fail(self) -> None:
        self.registry.revoke(self.session_id, self.owner)
        self._fault.set()

    def _spawn(self, work, *, critical: bool = False) -> asyncio.Task:
        async def guarded():
            try:
                # Create the coroutine only after this task starts. Immediate
                # capture cancellation must not leak an unawaited audio pump.
                await work()
                if critical and not self._closed:
                    self._fail()
            except asyncio.CancelledError:
                raise
            except Exception as error:
                # Only code locations and type names: no exception text,
                # peer documents, credentials, transcript or audio payloads.
                self.trace.mark('moq.session_fault', error_type=type(error).__name__,
                    locations=[dict(function=item.name, line=item.lineno)
                               for item in traceback.extract_tb(error.__traceback__)[-4:]])
                self._fail()
        task = asyncio.create_task(guarded())
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)
        return task

    def start(self) -> None:
        self._check()
        if self._started:
            raise MoqSessionError()
        self._started = True
        self._spawn(self._writer, critical=True)
        self._spawn(self._application, critical=True)
        self._spawn(self._watchdog, critical=True)

    async def run(self) -> None:
        self.start()
        self._spawn(self._read_control, critical=True)
        try:
            await self._fault.wait()
        finally:
            await self.close()

    async def _read_control(self) -> None:
        async for raw in self.websocket:
            if raw.type != WSMsgType.TEXT:
                raise MoqSessionError()
            try:
                document = json.loads(raw.data, object_pairs_hook=_unique_object,
                                      parse_constant=lambda _: (_ for _ in ()).throw(MoqSessionError()))
                await self.receive(document)
            except (ValueError, TypeError, RecursionError):
                raise MoqSessionError() from None

    async def attach(self, peer: BridgePeer) -> None:
        self._check()
        if self.peer is not None or peer.session_id != self.session_id or peer.device_id != self.device_id:
            raise MoqSessionError()
        self.peer = peer

    def _queue(self, item: Outbound) -> None:
        self._check()
        try:
            self._out.put_nowait(item)
        except asyncio.QueueFull:
            self._fail()
            raise MoqSessionError() from None

    async def send(self, message_type: str, payload: dict | None = None) -> None:
        if not self._current_intent():
            return
        guard = ACTION_CURRENT.get() if message_type == 'action.invoke' else None
        if guard is not None and not guard():
            raise ConnectionError('action capture retired')
        self._queue(Outbound('wss', message_type, dict(payload or {}), is_current=guard))

    def _current_intent(self) -> bool:
        intent = _INTENT.get()
        return intent is None or intent[0] is not self or intent[1] == self._intent

    def _ipc(self, kind: str, fields: dict | None = None, *, pcm: bytes = b'',
             response: Response | None = None, receipt: bool = False) -> asyncio.Future | None:
        if self.peer is None:
            raise MoqSessionError()
        future = asyncio.get_running_loop().create_future() if receipt else None
        self._queue(Outbound('ipc', kind, dict(fields or {}), pcm, response, future))
        return future

    async def _writer(self) -> None:
        while True:
            item = await self._out.get()
            try:
                self._check()
                if item.is_current is not None and not item.is_current():
                    future = self._pending_actions.get(item.payload.get('request_id'))
                    if future is not None and not future.done():
                        future.set_exception(ConnectionError('action capture retired'))
                    continue
                if item.response is not None and (item.response is not self._response or item.response.cancelled):
                    continue
                async with asyncio.timeout(2):
                    if item.channel == 'ipc':
                        if self.peer is None:
                            raise MoqSessionError()
                        await self.peer.send(item.kind, item.payload, item.pcm)
                    else:
                        self.sequence += 1
                        document = dict(v=1, type=item.kind, seq=self.sequence,
                                        session_id=self.session_id, device_id=self.device_id,
                                        payload=item.payload)
                        raw = json.dumps(document, separators=(',', ':'), allow_nan=False)
                        if len(raw.encode()) > 16384:
                            raise MoqSessionError()
                        if item.kind == 'playback.end' and item.response is not None:
                            item.response.end_sent = True
                        await self.websocket.send_str(raw)
                if item.sent is not None and not item.sent.done():
                    item.sent.set_result(None)
            finally:
                if item.sent is not None and not item.sent.done():
                    item.sent.cancel()

    def _event(self, kind: str, payload: Any, capture: Capture | None = None) -> None:
        try:
            self._app.put_nowait((kind, payload, capture, self._intent if kind.startswith('listen.') or kind in {'conversation.text', 'capture.failed'} else None))
        except asyncio.QueueFull:
            self._fail()
            raise MoqSessionError() from None

    async def _application(self) -> None:
        while True:
            kind, payload, capture, intent = await self._app.get()
            self._check()
            if capture is not None and capture is not self._capture:
                continue
            if intent is not None and intent != self._intent:
                continue
            if kind == 'capture.failed' and (self._capture is not None or self._pending_start
                    or self._response_context is not None or self._pending_context is not None):
                continue  # A queued failure cannot cancel a newer turn.
            # Application/provider setup may take longer than an IPC callback.
            # It never blocks native framing, liveness or the bounded writer.
            token = _INTENT.set((self, intent) if intent is not None else None)
            try:
                async with asyncio.timeout(15 if kind == 'identified' else 5 if kind == 'conversation.text' else 2):
                    if kind == 'identified':
                        await self.on_identified(self, self.device_id, payload)
                        self._identified = True
                        self._ready()
                    elif kind == 'audio':
                        await self.on_audio(self.device_id, payload)
                    else:
                        await self.on_event(self.device_id, kind, payload)
            finally:
                _INTENT.reset(token)

    def _ready(self) -> None:
        if self._identified and self._native_ready and self._watch_ready and not self.connected.is_set():
            self.connected.set()
            self.trace.mark('moq.session_ready')
            self._event('connected', {})

    async def receive(self, document: dict) -> None:
        self._check()
        if (not isinstance(document, dict) or set(document) != {'v', 'type', 'seq', 'session_id', 'device_id', 'payload'}
                or type(document['v']) is not int or document['v'] != 1
                or type(document['seq']) is not int or document['seq'] != self._received + 1
                or document['seq'] >= 2**53 or document['session_id'] != self.session_id
                or document['device_id'] != self.device_id or not isinstance(document['payload'], dict)
                or not isinstance(document['type'], str)):
            raise MoqSessionError()
        self._received = document['seq']
        kind, payload = document['type'], document['payload']
        if kind == 'hello':
            if (self._hello or payload.get('device_id') != self.device_id
                    or payload.get('transport') != 'moq' or not isinstance(payload.get('board'), str)
                    or not 1 <= len(payload['board']) <= 32 or not isinstance(payload.get('capabilities'), dict)):
                raise MoqSessionError()
            self._hello = True
            self.board, self.capabilities = payload['board'], dict(payload['capabilities'])
            self._event('identified', payload)
            self._renewal_supported = payload['capabilities'].get('moq_renewal_v1') is True
            await self.send('welcome', dict(mode='live-agent', transport='moq-lite-05',
                                           audio='opus-16000-hang-mono', barge_in='touch'))
            self._ready()
            return
        if not self._hello:
            raise MoqSessionError()
        if kind in {'welcome.ack', 'peer.created'}:
            if payload:
                raise MoqSessionError()
            return
        if kind == 'watch.state':
            self._event(kind, payload)
            return
        if kind == 'peer.ready':
            if self._watch_ready:
                raise MoqSessionError()
            self._watch_ready = True
            self._ready()
            return
        if not self.connected.is_set():
            raise MoqSessionError()
        if kind == 'session.renew':
            if (not self._renewal_supported or self._renewal_pending is not None
                    or set(payload) != {'nonce', 'proof'}):
                raise MoqSessionError()
            renewed = self.registry.renew(self.session_id, self.owner, payload['nonce'], payload['proof'])
            self._renewal_pending, self._renewal_native_ack = renewed, False
            self._renewal_deadline = asyncio.get_running_loop().time()+3
            self._ipc('session.renew', dict(renewal_revision=renewed['revision'],
                      expires_unix_ms=renewed['expires_unix']*1000,
                      lease_ms=renewed['lease_seconds']*1000))
        elif kind == 'session.renewed':
            if (set(payload) != {'revision'} or type(payload['revision']) is not int
                    or self._renewal_pending is None or not self._renewal_native_ack
                    or payload['revision'] != self._renewal_pending['revision']):
                raise MoqSessionError()
            self.renewals_completed += 1
            self._renewal_pending = None
            self.trace.mark('moq.session_renewed', revision=payload['revision'])
        elif kind in {'context.ready', 'context.rejected'}:
            self._context_receipt(kind, payload)
        elif kind == 'conversation.text':
            text = payload.get('text')
            if set(payload) != {'text'} or not isinstance(text, str) or not 1 <= len(text) <= 500 or '\0' in text:
                raise MoqSessionError()
            self._intent += 1
            self._retire_capture()
            self._event(kind, payload)
        elif kind == 'capture.started':
            identity = Identity.parse(payload)
            first = decimal(payload.get('first_group'), maximum=2**62 - 1600)
            start_id = decimal(payload.get('start_id', '0'))
            if int(identity.capture_id) <= self._highest_capture:
                return  # A delayed previous capture cannot replace the current one.
            if start_id:
                if start_id <= self._retired_start:
                    self._queue(Outbound('wss', 'capture.cancel', {**identity.fields(), 'start_id': str(start_id)}))
                    return
                if start_id != self._pending_start:
                    raise MoqSessionError()
                stop_requested = self._pending_stop
                self._pending_start = 0
            else:
                stop_requested = False  # Locally authorized guest capture.
                self._intent += 1
            self._retire_capture()
            self._highest_capture = int(identity.capture_id)
            capture = Capture(identity, first, asyncio.get_running_loop().time() + 33,
                              start_id=start_id, stop_requested=stop_requested)
            self._capture = capture
            self.trace.mark('moq.capture_started')
            self._ipc('capture.begin', {**identity.fields(), 'first_group': str(first)})
            self._event(kind, payload, capture)
        elif kind == 'capture.stopped':
            identity = Identity.parse(payload)
            capture = self._capture
            if capture is None or capture.identity != identity:
                return
            end = decimal(payload.get('end_group'), maximum=capture.first + 1600)
            samples = decimal(payload.get('samples'), maximum=31 * 16000)
            if (capture.end is not None or end <= capture.first or samples < capture.received
                    or decimal(payload.get('first_group')) != capture.first):
                raise MoqSessionError()
            capture.end, capture.samples = end, samples
            capture.deadline = asyncio.get_running_loop().time() + 5
            capture.stopped_payload = dict(payload)
            self._ipc('capture.end', {**identity.fields(), 'end_group': str(end), 'samples': str(samples)})
            # Do not notify STT of completion until the native terminal arrives.
        elif kind in {'playback.bound', 'playback.started', 'playback.finished'}:
            self._playback_receipt(kind, payload)
        elif kind == 'action.result':
            request = payload.get('request_id')
            if not isinstance(request, str) or len(request.encode()) > 64:
                raise MoqSessionError()
            future = self._pending_actions.get(request)
            if future is not None and not future.done():
                future.set_result(payload)
        elif kind == 'capture.failed':
            identity = Identity.parse(payload)
            start = decimal(payload.get('start_id'))
            capture = self._capture
            if capture is not None:
                if capture.identity != identity or capture.start_id != start:
                    return
            elif not self._pending_start or start != self._pending_start:
                return  # Includes the late acknowledgement of native cancellation.
            self._intent += 1
            self._retire_capture()
            self._event(kind, payload)
        elif kind in {'listen.requested', 'listen.cancelled'}:
            self._intent += 1
            self._retire_capture()
            self._event(kind, payload)
        elif kind in {'listen.finished', 'app.install.result', 'app.launch.result'}:
            self._event(kind, payload, self._capture if kind == 'listen.finished' else None)
        else:
            # No SDP/ICE, caller-asserted identity or unknown transport commands.
            raise MoqSessionError()

    async def native(self, peer: BridgePeer, packet: Packet) -> None:
        self._check()
        if peer is not self.peer:
            raise MoqSessionError()
        header, pcm = packet.header, packet.pcm
        kind = header['type']
        extras = set(header) - {'v', 'type', 'seq', 'session_id', 'pcm_bytes'}
        recovery_fields = {'concealed_samples', 'plc_samples', 'lost_groups', 'late_groups'}
        if kind == 'capture.ended' and recovery_fields <= extras:
            extras -= recovery_fields
        schemas = {
            'ping': set(), 'pong': set(), 'media.ready': set(),
            'session.renewed': {'renewal_revision'},
            'cancelled': {'capture_id', 'request_id', 'owner_token'},
            'capture.pcm': {'capture_id', 'request_id', 'owner_token'},
            'capture.failed': {'capture_id', 'request_id', 'owner_token'},
            'capture.ended': {'capture_id', 'request_id', 'owner_token', 'first_group', 'end_group', 'samples'},
            'playback.prepared': {'capture_id', 'request_id', 'owner_token', 'response_id', 'first_group', 'pts_us'},
            'playback.encoded': {'capture_id', 'request_id', 'owner_token', 'response_id', 'first_group', 'end_group', 'samples'},
        }
        if kind not in schemas or extras != schemas[kind] or (pcm and kind != 'capture.pcm'):
            raise MoqSessionError()
        if kind == 'session.renewed':
            if (type(header['renewal_revision']) is not int or self._renewal_pending is None
                    or self._renewal_native_ack
                    or header['renewal_revision'] != self._renewal_pending['revision']):
                raise MoqSessionError()
            self._renewal_native_ack = True
            self._queue(Outbound('wss', 'session.renewed', self._renewal_pending))
        elif kind == 'ping':
            self._ipc('pong')
        elif kind in {'pong', 'cancelled'}:
            return
        elif kind == 'media.ready':
            if self._native_ready:
                raise MoqSessionError()
            self._native_ready = True
            self._ready()
        else:
            identity = Identity.parse(header)
            capture = self._capture
            context = capture or self._response_context
            if context is None or identity != context.identity:
                return
            if kind == 'capture.failed':
                if capture is None or capture.validated.is_set():
                    raise MoqSessionError()
                self.trace.mark('moq.capture_failed', reason='loss_budget')
                self._intent += 1
                self._retire_capture()
                self._event('capture.failed', {**identity.fields(), 'reason': 'loss_budget'})
            elif kind == 'capture.pcm':
                if (capture is None or capture.validated.is_set() or not pcm or len(pcm) > 640 or len(pcm) % 2
                        or capture.received + len(pcm) // 2 > (capture.samples if capture.samples is not None else 31 * 16000)):
                    raise MoqSessionError()
                capture.received += len(pcm) // 2
                self._event('audio', pcm, capture)
            elif kind == 'capture.ended':
                if (capture is None or capture.validated.is_set() or capture.end is None
                        or decimal(header['first_group']) != capture.first
                        or decimal(header['end_group']) != capture.end
                        or decimal(header['samples']) != capture.samples
                        or capture.received != capture.samples):
                    raise MoqSessionError()
                concealed = decimal(header.get('concealed_samples', '0'), maximum=capture.samples)
                plc = decimal(header.get('plc_samples', '0'), maximum=concealed)
                lost = decimal(header.get('lost_groups', '0'), maximum=capture.end-capture.first)
                late = decimal(header.get('late_groups', '0'), maximum=3200)
                self.trace.mark('moq.capture_recovery', samples=capture.samples,
                                concealed_samples=concealed, plc_samples=plc,
                                lost_groups=lost, late_groups=late)
                capture.validated.set()
                self._event('capture.stopped', capture.stopped_payload, capture)
            else:
                response = self._matching_response(header)
                if response is None:
                    return
                first = decimal(header['first_group'], maximum=2**62 - 1)
                if kind == 'playback.prepared':
                    if response.prepared.is_set():
                        raise MoqSessionError()
                    pts = decimal(header['pts_us'], maximum=2**62 - 1)
                    response.first = first
                    response.prepared.set()
                    self._queue(Outbound('wss', 'playback.begin', {**response.fields(), 'first_group': str(first),
                                                               'pts_us': str(pts)}, response=response))
                else:
                    end = decimal(header['end_group'], maximum=first + 30002)
                    if (not response.end_queued or response.encoded.is_set() or first != response.first
                            or end <= first or decimal(header['samples']) != response.samples):
                        raise MoqSessionError()
                    response.end = end
                    response.encoded.set()
                    self._queue(Outbound('wss', 'playback.end', {**response.fields(), 'first_group': str(first),
                                         'end_group': str(end), 'samples': str(response.samples)}, response=response))

    def _matching_response(self, payload: dict) -> Response | None:
        identity, number = Identity.parse(payload), decimal(payload.get('response_id'))
        response = self._response
        if (response is None or response.cancelled or response.context.identity != identity
                or response.number != number):
            return None
        return response

    def _playback_receipt(self, kind: str, payload: dict) -> None:
        response = self._matching_response(payload)
        if response is None:
            return
        if (not response.prepared.is_set() or decimal(payload.get('first_group')) != response.first
                or type(payload.get('error')) is not int or payload['error'] != 0
                or type(payload.get('cancelled')) is not bool or payload['cancelled']):
            raise MoqSessionError()
        if kind == 'playback.bound':
            if response.bound.is_set() or decimal(payload.get('samples')) != 0:
                raise MoqSessionError()
            response.bound.set()
        elif kind == 'playback.started':
            if not response.bound.is_set():
                raise MoqSessionError()
        else:
            if (not response.end_sent or response.finished.is_set()
                    or decimal(payload.get('end_group')) != response.end
                    or decimal(payload.get('samples')) != response.samples):
                raise MoqSessionError()
            response.finished.set()

    async def start_capture(self, duration_ms: int = 30000) -> None:
        if not self._current_intent():
            return
        if not self.connected.is_set() or type(duration_ms) is not int or not 1 <= duration_ms <= 30000:
            raise MoqSessionError()
        if self._pending_start or self._capture is not None:
            self._retire_capture()
        self._highest_start += 1
        self._pending_start, self._pending_stop = self._highest_start, False
        self._start_deadline = asyncio.get_running_loop().time() + 3
        await self.send('capture.start', {'duration_ms': duration_ms, 'start_id': str(self._pending_start)})

    async def invoke_action(self, capability, arguments, idempotency_key, timeout=8.0):
        self._check()
        if len(self._pending_actions) >= 32:
            raise MoqSessionError()
        return await super().invoke_action(capability, arguments, idempotency_key, timeout)

    async def stop_capture(self) -> None:
        if not self._current_intent():
            return
        capture = self._capture
        if capture is not None and not capture.stop_requested and capture.end is None:
            capture.stop_requested = True
            await self.send('capture.stop', {**capture.identity.fields(), 'start_id': str(capture.start_id)})
        elif self._pending_start and not self._pending_stop:
            # PTT release can overtake the asynchronous capture.started event.
            # The watch matches this command to the pending start, not whatever
            # guest happens to own the microphone when it processes the stop.
            self._pending_stop = True
            await self.send('capture.stop', {'start_id': str(self._pending_start)})

    async def authorize_response(self, kind: str) -> ResponseContext:
        self._check()
        if kind not in {'text', 'background'} or not self.connected.is_set() or not self._current_intent():
            raise MoqSessionError()
        if self._pending_context is not None:
            raise ResponseContextBusy('response context pending')
        if kind == 'background' and ((self._capture is not None and not self._capture.validated.is_set())
                                    or (self._response is not None and not self._response.done.is_set())):
            raise ResponseContextBusy('voice operation active')
        self._retire_capture()
        self._highest_context_request += 1
        request = self._highest_context_request
        future = asyncio.get_running_loop().create_future()
        self._pending_context = (request, kind, future)
        accepted = False
        try:
            await self.send('context.request', {'context_request_id':str(request), 'kind':kind})
            context = await asyncio.wait_for(asyncio.shield(future), 3)
            if context is not self._response_context or not self._current_intent():
                raise ConnectionError('response context retired')
            accepted = True
            return context
        finally:
            if self._pending_context is not None and self._pending_context[0] == request:
                self._pending_context = None
                if not future.done():
                    future.cancel()
                if not accepted and not self._closed and not self._fault.is_set():
                    self._queue(Outbound('wss','context.cancel',{'context_request_id':str(request)}))
                    if self._response_context is not None and self._response_context.request_id == request:
                        context, self._response_context = self._response_context, None
                        self._ipc('cancel', context.identity.fields())

    def _context_receipt(self, kind, payload):
        request = decimal(payload.get('context_request_id'))
        expected = {'context_request_id','reason'} if kind == 'context.rejected' else {
            'context_request_id','context_id','request_id','owner_token','kind'}
        if not request or set(payload) != expected:
            raise MoqSessionError()
        pending = self._pending_context
        if pending is None or request != pending[0]:
            current = self._response_context
            if kind == 'context.ready' and current is not None and current.request_id == request:
                if (payload['context_id'] != current.identity.capture_id or payload['kind'] != current.kind
                        or payload['request_id'] != current.identity.request_id or payload['owner_token'] != current.identity.owner_token):
                    raise MoqSessionError()
                return
            if request > self._highest_context_request:
                raise MoqSessionError()
            if kind == 'context.ready':
                self._queue(Outbound('wss','context.cancel',{'context_request_id':str(request)}))
            return
        if pending[2].done():
            raise MoqSessionError()
        if kind == 'context.rejected':
            if payload['reason'] != 'busy':
                raise MoqSessionError()
            pending[2].set_exception(ResponseContextBusy('watch voice operation active'))
            return
        number = decimal(payload['context_id'])
        # The watch grants neutral host speech; a caller cannot claim a guest.
        if (number <= self._highest_capture or payload['kind'] != pending[1]
                or decimal(payload['request_id']) or decimal(payload['owner_token'])):
            raise MoqSessionError()
        identity = Identity(str(number), '0', '0')
        context = ResponseContext(identity, request, pending[1])
        context.ready.set()
        self._response_context = context
        self._highest_capture = number
        self._ipc('context.begin', identity.fields())
        pending[2].set_result(context)

    def begin_downlink(self) -> None:
        self._check()
        context = self._capture or self._response_context
        if context is None or (self._response is not None and not self._response.done.is_set()):
            raise MoqSessionError()
        self.downlink.begin_utterance()
        self._highest_response += 1
        response = Response(context, self._highest_response, self.downlink.generation)
        self._response = response
        response.task = self._spawn(lambda: self._pump(response))

    def enqueue_downlink(self, pcm: bytes, sample_rate: int) -> int:
        self._check()
        try:
            if type(sample_rate) is not int or not 1 <= sample_rate <= 192000:
                raise ValueError('invalid MoQ input sample rate')
            if self.downlink.utterance_samples + len(pcm) // 2 * 16000 // sample_rate > 600 * 16000:
                raise BufferError('MoQ response duration exceeded')
            return self.downlink.enqueue_pcm(pcm, sample_rate)
        except Exception:
            self.clear_downlink()
            raise

    def end_downlink(self) -> int:
        self._check()
        try:
            result = self.downlink.end_utterance()
            if self.downlink.utterance_samples > 600 * 16000:
                raise BufferError('MoQ response duration exceeded')
            return result
        except Exception:
            self.clear_downlink()
            raise

    async def _pump(self, response: Response) -> None:
        try:
            ready = response.context.validated if isinstance(response.context,Capture) else response.context.ready
            await asyncio.wait_for(ready.wait(), 33)
            await self._ipc('playback.begin', response.fields(), response=response, receipt=True)
            await asyncio.wait_for(response.prepared.wait(), 5)
            await asyncio.wait_for(response.bound.wait(), 5)
            await self._ipc('playback.bound', response.fields(), response=response, receipt=True)
            async with asyncio.timeout(610):
                while True:
                    packet = await self.downlink.read(response.generation)
                    if packet is None:
                        break
                    response.samples += len(packet.data) // 2
                    await self._ipc('playback.pcm', response.fields(), pcm=packet.data, response=response, receipt=True)
            response.end_queued = True
            await self._ipc('playback.end', response.fields(), response=response, receipt=True)
            await asyncio.wait_for(response.encoded.wait(), 5)
            await asyncio.wait_for(response.finished.wait(), 5)
        except PacingOverrun:
            # A media starvation budget failure cancels this response, not its
            # authenticated session or the next response's fresh pacing clock.
            self.trace.mark('moq.playback_pacing_overrun')
            if self._response is response:
                self.clear_downlink()
        finally:
            response.done.set()

    async def resume_after_downlink(self) -> bool:
        response = self._response
        if response is not None:
            await response.done.wait()
            self._check()
            if not response.cancelled and not response.finished.is_set():
                raise MoqSessionError()
            return not response.cancelled and response.finished.is_set() and self._response is response
        return False

    def clear_downlink(self) -> None:
        if not self._current_intent() and not self._closed:
            return
        response = self._response
        self.downlink.clear()
        if response is None or response.cancelled or response.done.is_set():
            return
        response.cancelled = True
        response.done.set()
        if response.task is not None:
            response.task.cancel()
        if not self._closed and not self._fault.is_set():
            self._ipc('playback.cancel', response.fields())
            self._queue(Outbound('wss', 'playback.cancel', response.fields()))

    def _retire_capture(self) -> None:
        self.clear_downlink()
        pending, self._pending_context = self._pending_context, None
        if pending is not None:
            self._queue(Outbound('wss','context.cancel',{'context_request_id':str(pending[0])}))
            if not pending[2].done():
                pending[2].set_exception(ConnectionError('response context retired'))
        context, self._response_context = self._response_context, None
        if context is not None:
            self._ipc('cancel', context.identity.fields())
            self._queue(Outbound('wss','context.cancel',{'context_request_id':str(context.request_id)}))
        if self._pending_start:
            self._retired_start = self._pending_start
            self._queue(Outbound('wss', 'capture.cancel', {'start_id': str(self._pending_start)}))
            self._pending_start = 0
        capture = self._capture
        self._capture = None
        if capture is not None:
            self._retired_start = max(self._retired_start, capture.start_id)
            self._ipc('cancel', capture.identity.fields())
            self._queue(Outbound('wss', 'capture.cancel', {**capture.identity.fields(), 'start_id': str(capture.start_id)}))

    async def _watchdog(self) -> None:
        while True:
            self._check()
            now = asyncio.get_running_loop().time()
            if not self.connected.is_set() and now >= self._startup_deadline:
                raise MoqSessionError()
            if self._pending_start and now >= self._start_deadline:
                raise MoqSessionError()
            if self._capture is not None and not self._capture.validated.is_set() and now >= self._capture.deadline:
                raise MoqSessionError()
            if self._renewal_pending is not None:
                if now >= self._renewal_deadline:
                    raise MoqSessionError()
            elif self.connected.is_set() and self._renewal_supported:
                nonce = self.registry.renewal_challenge(self.session_id, self.owner)
                if nonce is not None:
                    self._queue(Outbound('wss', 'session.challenge', {'nonce': nonce}))
            await asyncio.sleep(0.1)

    async def close(self, *, code: int = 1000, message: bytes = b'') -> None:
        if self._closed:
            return
        self._closed = True
        self._fail()
        self.clear_downlink()
        self._fail_pending_actions()
        if self._pending_context is not None and not self._pending_context[2].done():
            self._pending_context[2].set_exception(ConnectionError('response context session closed'))
        self._pending_context = None
        self._response_context = None
        self._renewal_pending = None
        tasks = self._tasks - {asyncio.current_task()}
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        while not self._out.empty():
            item = self._out.get_nowait()
            if item.sent is not None and not item.sent.done():
                item.sent.cancel()
        while not self._app.empty():
            self._app.get_nowait()
        if not self.websocket.closed:
            try:
                await asyncio.wait_for(self.websocket.close(code=code, message=message), 2)
            except (TimeoutError, ConnectionError):
                pass
