"""Session ordering/ownership tests; synthetic audio and public fixture keys only."""
import asyncio
import json

import pytest
import pytest_asyncio

from doodad_agent.metrics import LatencyTrace
from doodad_agent.audio import PacingOverrun
from doodad_agent.moq_auth import AuthorizationError, GrantRegistry, bootstrap_proof, renewal_proof
from doodad_agent.moq_ipc import Packet
from doodad_agent.moq_session import MoqSession, MoqSessionError
from doodad_agent.session import ACTION_CURRENT, DownlinkUtteranceBinding

DEVICE = 'watch-ultra-session-test'
KEY = bytes(range(32))
ID = dict(capture_id='71', request_id='72', owner_token='73')


class Socket:
    closed = False

    def __init__(self):
        self.sent = asyncio.Queue()

    async def send_str(self, text):
        await self.sent.put(json.loads(text))

    async def close(self, **_):
        self.closed = True


class Peer:
    def __init__(self, sid):
        self.session_id, self.device_id = sid, DEVICE
        self.sent = asyncio.Queue()

    async def send(self, kind, fields, pcm):
        self.sent.put_nowait((kind, fields, pcm))


class Harness:
    def __init__(self):
        self.registry = GrantRegistry({DEVICE: KEY})
        nonce = self.registry.challenge(DEVICE)
        grant = self.registry.issue(DEVICE, nonce, bootstrap_proof(KEY, DEVICE, nonce))
        owner = object()
        self.registry.activate_control(grant.control_token, owner)
        self.ws, self.peer = Socket(), Peer(grant.session_id)
        self.registry.attach_media(grant.media_token, self.peer)
        self.events, self.seq = [], 0
        self.event_gate = asyncio.Event()
        self.event_gate.set()

        async def event(_, kind, payload):
            await self.event_gate.wait()
            self.events.append((kind, payload))

        async def audio(_, pcm):
            self.events.append(('audio', pcm))

        async def identify(_, device, payload):
            await event(device, 'identified', payload)

        self.session = MoqSession(self.ws, LatencyTrace(), audio, event, identify,
                                  registry=self.registry, session_id=grant.session_id, owner=owner)

    async def watch(self, kind, payload=None, **overrides):
        self.seq += 1
        document = dict(v=1, seq=self.seq, type=kind, device_id=DEVICE,
                        session_id=self.peer.session_id, payload=payload or {})
        document.update(overrides)
        await self.session.receive(document)

    async def native(self, kind, fields=None, pcm=b''):
        await self.session.native(self.peer, Packet(dict(v=1, type=kind, seq=1,
                          session_id=self.peer.session_id, pcm_bytes=len(pcm), **(fields or {})), pcm))

    async def next_ipc(self, kind):
        async with asyncio.timeout(1):
            while True:
                item = await self.peer.sent.get()
                if item[0] == kind:
                    return item

    async def next_control(self, kind):
        async with asyncio.timeout(1):
            while True:
                item = await self.ws.sent.get()
                if item['type'] == kind:
                    return item['payload']

    async def capture(self, samples=537):
        await self.watch('capture.started', {**ID, 'first_group': '0'})
        await self.next_ipc('capture.begin')
        await self.watch('capture.stopped', {**ID, 'first_group': '0', 'end_group': '3', 'samples': str(samples)})
        await self.next_ipc('capture.end')
        for offset in range(0, samples, 320):
            await self.native('capture.pcm', ID, b'\x03\x00' * min(320, samples-offset))
        await self.native('capture.ended', {**ID, 'first_group': '0', 'end_group': '3', 'samples': str(samples)})

    async def prepare(self, number='1', first='0'):
        await self.next_ipc('playback.begin')
        await self.native('playback.prepared', {**ID, 'response_id': number, 'first_group': first, 'pts_us': '1234567'})
        return await self.next_control('playback.begin')

    async def bind(self, number='1', first='0'):
        await self.watch('playback.bound', {**ID, 'response_id': number, 'first_group': first,
                                          'samples': '0', 'error': 0, 'cancelled': False})
        await self.next_ipc('playback.bound')


@pytest_asyncio.fixture
async def harness():
    h = Harness()
    h.session.start()
    await h.session.attach(h.peer)
    await h.watch('hello', dict(device_id=DEVICE, board='t-watch-ultra', transport='moq', capabilities={'moq_renewal_v1': True}))
    await h.watch('peer.ready')
    assert not h.session.connected.is_set()
    await h.native('media.ready')
    await h.next_control('welcome')
    assert h.session.connected.is_set()
    try:
        yield h
    finally:
        await h.session.close()
        assert not h.session._tasks


@pytest.mark.asyncio
async def test_renewal_waits_for_native_then_watch_ack_without_retiring_response(harness):
    h = harness
    await h.capture()
    capture = h.session._capture
    h.session.begin_downlink()
    response = h.session._response
    mono, wall = h.registry._clock()+151, h.registry._wall()+151
    h.registry._clock, h.registry._wall = lambda: mono, lambda: wall
    challenge = await h.next_control('session.challenge')
    await h.watch('session.renew', dict(nonce=challenge['nonce'], proof=renewal_proof(
        KEY, DEVICE, h.session.session_id, challenge['nonce'])))
    _, fields, pcm = await h.next_ipc('session.renew')
    assert fields['renewal_revision'] == 1 and fields['lease_ms'] == 300000 and not pcm
    assert not any(item['type']=='session.renewed' for item in list(h.ws.sent._queue))
    assert h.session.renewals_completed == 0
    await h.native('session.renewed', {'renewal_revision': 1})
    renewed = await h.next_control('session.renewed')
    assert renewed['nonce'] == challenge['nonce'] and renewed['revision'] == 1
    assert h.session.renewals_completed == 0
    await h.watch('session.renewed', {'revision': 1})
    assert h.session.renewals_completed == 1
    assert h.session._capture is capture and h.session._response is response and not response.cancelled
    assert h.session.connected.is_set()
    with pytest.raises(MoqSessionError):
        await h.watch('session.renewed', {'revision': 1})


@pytest.mark.asyncio
@pytest.mark.parametrize('kind', ['session.renew', 'session.renewed'])
async def test_unsolicited_renewal_or_ack_is_rejected(harness, kind):
    with pytest.raises((MoqSessionError, AuthorizationError)):
        await harness.watch(kind, {'nonce': '0'*64, 'proof': '0'*64} if kind=='session.renew' else {'revision': 1})
    assert harness.session.renewals_completed == 0


@pytest.mark.asyncio
async def test_missing_native_renewal_ack_retires_session_without_success(harness):
    h = harness
    mono, wall = h.registry._clock()+151, h.registry._wall()+151
    h.registry._clock, h.registry._wall = lambda: mono, lambda: wall
    challenge = await h.next_control('session.challenge')
    await h.watch('session.renew', dict(nonce=challenge['nonce'], proof=renewal_proof(
        KEY, DEVICE, h.session.session_id, challenge['nonce'])))
    await h.next_ipc('session.renew')
    h.session._renewal_deadline = asyncio.get_running_loop().time()  # Exact deadline, without a three-second sleep.
    await asyncio.wait_for(h.session._fault.wait(), 1)
    assert h.session.renewals_completed == 0 and not h.registry.valid(h.session.session_id,h.session.owner)
    assert not any(item['type']=='session.renewed' for item in list(h.ws.sent._queue))


@pytest.mark.asyncio
async def test_native_capture_failure_discards_partial_turn_and_preserves_session(harness):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.next_ipc('capture.begin')
    await h.native('capture.pcm', ID, b'\x03\x00' * 320)
    # Do not yield: the PCM is queued but has not been delivered to STT.
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x04\x00' * 537, 16000)
    h.session.end_downlink()
    await h.native('capture.failed', ID)
    cancelled = await h.next_control('capture.cancel')
    assert all(cancelled[k] == v for k, v in ID.items())
    await h.next_ipc('cancel')
    await asyncio.sleep(.01)
    assert not any(kind in {'audio', 'capture.stopped', 'disconnected'} for kind, _ in h.events)
    assert [payload.get('reason') for kind, payload in h.events if kind == 'capture.failed'] == ['loss_budget']
    assert h.session.connected.is_set() and not h.ws.closed
    # A new capture survives every late callback from the failed one.
    fresh = {**ID, 'capture_id': '74'}
    await h.watch('capture.started', {**fresh, 'first_group': '20'})
    await h.next_ipc('capture.begin')
    await h.watch('capture.failed', {**ID, 'start_id': '0'})
    await h.watch('capture.stopped', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    await h.native('capture.failed', ID)
    await h.native('capture.pcm', ID, b'\x03\x00' * 320)
    await h.native('capture.ended', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    await h.native('capture.pcm', fresh, b'\x05\x00' * 320)
    await h.watch('capture.stopped', {**fresh, 'first_group': '20', 'end_group': '22', 'samples': '320'})
    await h.native('capture.ended', {**fresh, 'first_group': '20', 'end_group': '22', 'samples': '320'})
    await asyncio.sleep(.01)
    assert [pcm for kind, pcm in h.events if kind == 'audio'] == [b'\x05\x00' * 320]
    assert len([1 for kind, _ in h.events if kind == 'capture.stopped']) == 1
    assert len([1 for kind, _ in h.events if kind == 'capture.failed']) == 1
    assert h.session._capture.validated.is_set() and not h.session._fault.is_set()


@pytest.mark.asyncio
async def test_queued_capture_failure_cannot_cancel_immediate_replacement(harness):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.native('capture.failed', ID)
    fresh = {**ID, 'capture_id': '74'}
    await h.watch('capture.started', {**fresh, 'first_group': '20'})
    await asyncio.sleep(.01)
    assert not any(kind == 'capture.failed' for kind, _ in h.events)
    assert h.session._capture.identity.capture_id == '74'


@pytest.mark.asyncio
async def test_watch_failure_correlates_pending_start_and_rejects_unbound_failure(harness):
    h = harness
    await h.session.start_capture(1000)
    start = await h.next_control('capture.start')
    await h.watch('capture.failed', {**ID, 'start_id': '0'})
    assert h.session._pending_start
    await h.watch('capture.failed', {**ID, 'start_id': start['start_id']})
    await asyncio.sleep(.01)
    assert not h.session._pending_start
    assert len([1 for kind, _ in h.events if kind == 'capture.failed']) == 1
    with pytest.raises(MoqSessionError):
        await h.watch('capture.failed', {})


@pytest.mark.asyncio
async def test_native_tail_precedes_stt_completion_and_bound_precedes_pcm(harness):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.watch('capture.stopped', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    h.session.begin_downlink()  # Early TTS may spool but cannot race the tail.
    assert h.session.enqueue_downlink(b'\x04\x00' * 537, 16000) == 537
    h.session.end_downlink()
    await asyncio.sleep(0)
    assert 'capture.stopped' not in [kind for kind, _ in h.events]
    assert 'playback.begin' not in [item[0] for item in list(h.peer.sent._queue)]
    for samples in (320, 217):
        await h.native('capture.pcm', ID, b'\x03\x00' * samples)
    await h.native('capture.ended', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    begin = await h.prepare()
    assert begin['pts_us'] == '1234567'
    assert 'playback.pcm' not in [item[0] for item in list(h.peer.sent._queue)]
    await h.bind()
    one = await h.next_ipc('playback.pcm')
    two = await h.next_ipc('playback.pcm')
    assert one[2] + two[2] == b'\x04\x00' * 537
    await h.next_ipc('playback.end')
    wait = asyncio.create_task(h.session.resume_after_downlink())
    await h.native('playback.encoded', {**ID, 'response_id': '1', 'first_group': '0',
                                      'end_group': '3', 'samples': '537'})
    await h.next_control('playback.end')
    assert not wait.done()  # Neither spool drain nor encoded tail is playback.
    await h.watch('playback.finished', {**ID, 'response_id': '1', 'first_group': '0',
                                      'end_group': '3', 'samples': '537', 'error': 0, 'cancelled': False})
    await asyncio.wait_for(wait, 1)
    audio_end = [kind for kind, _ in h.events if kind in {'audio', 'capture.stopped'}]
    assert audio_end == ['audio', 'audio', 'capture.stopped']


@pytest.mark.asyncio
@pytest.mark.parametrize('fault', ['wrong_device', 'wrong_session', 'bool_sequence', 'replay', 'transport', 'unknown'])
async def test_control_rejects_identity_sequence_and_transport_confusion(harness, fault):
    h = harness
    changes = {'wrong_device': dict(device_id='watch-someone-else'),
               'wrong_session': dict(session_id='0'*32), 'bool_sequence': dict(seq=True),
               'replay': dict(seq=h.seq), 'transport': dict(type='sdp'),
               'unknown': dict(extra='untrusted')}
    with pytest.raises(MoqSessionError):
        await h.watch('watch.state', {}, **changes[fault])


@pytest.mark.asyncio
@pytest.mark.parametrize('fault', ['sample_count', 'range', 'before_bound', 'cancelled', 'error'])
async def test_playback_receipts_cannot_invent_completion(harness, fault):
    h = harness
    await h.capture()
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x04\x00' * 537, 16000)
    h.session.end_downlink()
    await h.prepare()
    if fault != 'before_bound':
        await h.bind()
        await h.next_ipc('playback.end')
        await h.native('playback.encoded', {**ID, 'response_id': '1', 'first_group': '0',
                                          'end_group': '3', 'samples': '537'})
        await h.next_control('playback.end')
    receipt = {**ID, 'response_id': '1', 'first_group': '0', 'end_group': '3',
               'samples': '537', 'error': 0, 'cancelled': False}
    receipt.update({'sample_count': dict(samples='536'), 'range': dict(end_group='4'),
                    'cancelled': dict(cancelled=True), 'error': dict(error=5)}.get(fault, {}))
    with pytest.raises(MoqSessionError):
        await h.watch('playback.finished', receipt)
    assert not h.session._response.finished.is_set()


@pytest.mark.asyncio
async def test_pacing_overrun_cancels_only_response_and_allows_fresh_clock(harness):
    h = harness
    await h.capture()
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x04\x00'*320, 16000)
    h.session.end_downlink()
    async def overdue(): raise PacingOverrun()
    h.session.downlink._pacer.wait = overdue
    await h.prepare()
    await h.bind()
    await h.next_ipc('playback.cancel')
    await h.next_control('playback.cancel')
    assert await h.session.resume_after_downlink() is False
    assert h.session.connected.is_set() and not h.session._fault.is_set()
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x05\x00'*217, 16000)
    h.session.end_downlink()
    await h.prepare('2', '1')
    await h.bind('2', '1')
    _, _, pcm = await h.next_ipc('playback.pcm')
    assert pcm == b'\x05\x00'*217
    assert not h.session._fault.is_set()


@pytest.mark.asyncio
async def test_cancel_held_pcm_and_ignore_stale_reply_without_harming_replacement(harness):
    h = harness
    await h.capture()
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x04\x00'*320, 16000)
    h.session.end_downlink()
    entered, release = asyncio.Event(), asyncio.Event()
    original = h.session.downlink._pacer.wait
    async def held():
        entered.set()
        await release.wait()
        return await original()
    h.session.downlink._pacer.wait = held
    await h.prepare()
    await h.bind()
    await asyncio.wait_for(entered.wait(), 1)
    h.session.clear_downlink()
    await h.next_ipc('playback.cancel')
    assert await h.session.resume_after_downlink() is False
    h.session.downlink._pacer.wait = original
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x05\x00'*217, 16000)
    h.session.end_downlink()
    await h.prepare('2', '1')
    await h.watch('playback.bound', {**ID, 'response_id': '1', 'first_group': '0',
                                    'samples': '0', 'error': 0, 'cancelled': False})
    assert not h.session._response.bound.is_set()
    await h.bind('2', '1')
    packet = await h.next_ipc('playback.pcm')
    assert packet[2] == b'\x05\x00'*217
    assert packet[1]['response_id'] == '2'
    assert h.registry.valid(h.session.session_id, h.session.owner)


@pytest.mark.asyncio
async def test_replaced_capture_discards_queued_audio_and_stale_native_completion(harness):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.native('capture.pcm', ID, b'\x03\x00'*320)
    newer = {**ID, 'capture_id': '74', 'owner_token': '75'}
    await h.watch('capture.started', {**newer, 'first_group': '9'})
    await h.native('capture.ended', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '320'})
    await h.next_ipc('cancel')
    assert h.session._capture.identity.owner_token == '75'
    assert not h.session._capture.validated.is_set()
    assert 'audio' not in [kind for kind, _ in h.events]


@pytest.mark.asyncio
async def test_action_result_and_disconnect_preserve_shared_futures(harness):
    h = harness
    action = asyncio.create_task(h.session.invoke_action('display.text', {'text': 'test'}, 'a'*90))
    command = await h.next_control('action.invoke')
    await h.watch('action.result', dict(request_id=command['request_id'], ok=True, result={'shown': True}))
    assert await action == {'shown': True, 'duplicate': False}
    pending = asyncio.create_task(h.session.invoke_action('display.text', {}, 'pending'))
    await h.next_control('action.invoke')
    await h.session.close()
    with pytest.raises(ConnectionError):
        await pending


@pytest.mark.asyncio
async def test_cancelled_action_waiting_for_writer_never_reaches_watch(harness):
    h = harness
    entered, release = asyncio.Event(), asyncio.Event()
    original_send = h.ws.send_str
    async def held_send(raw):
        if json.loads(raw)['type'] == 'test.writer_gate':
            entered.set()
            await release.wait()
        await original_send(raw)
    h.ws.send_str = held_send
    await h.session.send('test.writer_gate', {})
    await asyncio.wait_for(entered.wait(), 1)
    operation = {'live': True}
    token = ACTION_CURRENT.set(lambda: operation['live'])
    try:
        pending = asyncio.create_task(h.session.invoke_action('display.text', {}, 'cancelled-queued-action'))
    finally:
        ACTION_CURRENT.reset(token)
    async with asyncio.timeout(1):
        while not h.session._out.qsize(): await asyncio.sleep(0)
    operation['live'] = False
    release.set()
    with pytest.raises(ConnectionError): await asyncio.wait_for(pending, 1)
    assert not any(item['type'] == 'action.invoke' for item in h.ws.sent._queue)
    assert not h.session._pending_actions
    assert h.registry.valid(h.session.session_id, h.session.owner)


async def response_context(h, source='text', number='81'):
    task = asyncio.create_task(h.session.authorize_response(source))
    request = await h.next_control('context.request')
    receipt = dict(context_request_id=request['context_request_id'], kind=source,
                   context_id=number, request_id='0', owner_token='0')
    await h.watch('context.ready', receipt)
    context = await task
    await h.next_ipc('context.begin')
    return context, receipt


@pytest.mark.asyncio
async def test_output_only_context_authorizes_playback_without_any_capture(harness):
    h = harness
    context, receipt = await response_context(h)
    assert h.session._capture is None and context.ready.is_set()
    assert not any(kind in ('audio','capture.started','capture.stopped') for kind, _ in h.events)
    h.session.begin_downlink()
    begin = await h.next_ipc('playback.begin')
    assert begin[1]['capture_id'] == '81'
    assert not any(kind.startswith('capture.') for kind, _, _ in h.peer.sent._queue)
    # A duplicate acknowledgement must not cancel the live context.
    await h.watch('context.ready', receipt)
    assert h.session._response_context is context
    assert not any(item['type']=='context.cancel' for item in h.ws.sent._queue)


@pytest.mark.asyncio
async def test_cancelled_context_request_rejects_late_ack_without_native_input(harness):
    h = harness
    task = asyncio.create_task(h.session.authorize_response('text'))
    request = await h.next_control('context.request')
    await h.watch('listen.cancelled')
    with pytest.raises(ConnectionError): await task
    await h.watch('context.ready',dict(context_request_id=request['context_request_id'],kind='text',
                                    context_id='81',request_id='0',owner_token='0'))
    assert h.session._response_context is None and h.session._capture is None
    assert not any(kind in ('context.begin','capture.begin') for kind, _, _ in h.peer.sent._queue)


@pytest.mark.asyncio
async def test_watch_busy_keeps_microphone_off_and_context_unset(harness):
    h = harness
    task = asyncio.create_task(h.session.authorize_response('background'))
    request = await h.next_control('context.request')
    await h.watch('context.rejected',dict(context_request_id=request['context_request_id'],reason='busy'))
    with pytest.raises(ConnectionError): await task
    assert h.session._response_context is None and h.session._capture is None
    assert h.registry.valid(h.session.session_id,h.session.owner)


@pytest.mark.asyncio
async def test_output_context_cannot_be_used_to_inject_microphone_samples(harness):
    h = harness
    context, _ = await response_context(h)
    with pytest.raises(MoqSessionError):
        await h.native('capture.pcm',context.identity.fields(),b'\0\0'*160)
    assert not any(kind=='audio' for kind, _ in h.events)


@pytest.mark.asyncio
async def test_new_capture_retires_output_context_and_stale_cancel_is_scoped(harness):
    h = harness
    context, receipt = await response_context(h)
    await h.watch('capture.started',{**ID,'capture_id':'82','first_group':'0'})
    await h.next_ipc('cancel')
    await h.next_ipc('capture.begin')
    assert h.session._response_context is None and h.session._capture.identity.capture_id=='82'
    await h.watch('context.ready',receipt)
    assert h.session._capture.identity.capture_id=='82'


@pytest.mark.asyncio
async def test_context_caller_cancellation_retires_grant_even_after_receipt(harness):
    h = harness
    task = asyncio.create_task(h.session.authorize_response('text'))
    request = await h.next_control('context.request')
    await h.watch('context.ready',dict(context_request_id=request['context_request_id'],kind='text',
                                    context_id='81',request_id='0',owner_token='0'))
    task.cancel()
    with pytest.raises(asyncio.CancelledError): await task
    assert h.session._response_context is None and h.session._pending_context is None


@pytest.mark.asyncio
async def test_bounded_control_queue_retires_on_overflow(harness):
    h = harness
    for _ in range(32):
        await h.session.send('agent.state', {})
    with pytest.raises(MoqSessionError):
        await h.session.send('agent.state', {})
    assert not h.registry.valid(h.session.session_id, h.session.owner)


@pytest.mark.asyncio
async def test_capture_count_mismatch_cannot_finish_stt(harness):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.watch('capture.stopped', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    await h.native('capture.pcm', ID, b'\x03\x00'*320)
    with pytest.raises(MoqSessionError):
        await h.native('capture.ended', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '537'})
    assert not h.session._capture.validated.is_set()


@pytest.mark.asyncio
async def test_old_drain_cannot_release_new_utterance_binding():
    class Session:
        def __init__(self):
            self.waiting, self.drained = asyncio.Event(), asyncio.Event()
            self.pcm = bytearray()
        def begin_downlink(self): pass
        def enqueue_downlink(self, pcm, sample_rate): self.pcm.extend(pcm); return len(pcm)//2
        def clear_downlink(self): pass
        async def resume_after_downlink(self):
            self.waiting.set()
            await self.drained.wait()
    session = Session()
    binding = DownlinkUtteranceBinding()
    binding.begin(session)
    old = asyncio.create_task(binding.wait_for_playback(lambda: session))
    await session.waiting.wait()
    binding.begin(session)
    session.drained.set()
    await old
    assert binding.enqueue(session, b'\x01\x00'*217, 16000) == 217
    assert session.pcm == b'\x01\x00'*217


@pytest.mark.asyncio
async def test_ptt_release_before_capture_ack_stops_only_the_pending_start(harness):
    h = harness
    await h.session.start_capture()
    command = await h.next_control('capture.start')
    await h.session.stop_capture()
    stop = await h.next_control('capture.stop')
    assert stop == {'start_id': command['start_id']}
    await h.watch('capture.started', {**ID, 'first_group': '0', 'start_id': command['start_id']})
    assert h.session._capture.stop_requested
    await h.next_ipc('capture.begin')


@pytest.mark.asyncio
async def test_cancelled_start_ack_cannot_authorize_audio_for_a_new_ptt(harness):
    h = harness
    await h.session.start_capture()
    first = await h.next_control('capture.start')
    await h.watch('listen.cancelled')
    await h.next_control('capture.cancel')
    await h.session.start_capture()
    second = await h.next_control('capture.start')
    await h.watch('capture.started', {**ID, 'first_group': '0', 'start_id': first['start_id']})
    assert h.session._capture is None
    await h.native('capture.pcm', ID, b'\x05\x00'*320)
    newer = {**ID, 'capture_id': '74'}
    await h.watch('capture.started', {**newer, 'first_group': '3', 'start_id': second['start_id']})
    begin = await h.next_ipc('capture.begin')
    assert begin[1]['capture_id'] == '74'
    assert h.session._capture.received == 0


@pytest.mark.asyncio
async def test_ptt_cancel_overtaking_application_callback_cannot_rearm_microphone(harness):
    h = harness
    waiting, release, cancelled = asyncio.Event(), asyncio.Event(), asyncio.Event()
    async def event(_, kind, payload):
        if kind == 'listen.requested':
            waiting.set()
            await release.wait()
            await h.session.start_capture()
        elif kind == 'listen.cancelled':
            cancelled.set()
    h.session.on_event = event
    await h.watch('listen.requested')
    await asyncio.wait_for(waiting.wait(), 1)
    await h.watch('listen.cancelled')
    release.set()
    await asyncio.wait_for(cancelled.wait(), 1)
    assert h.session._pending_start == 0
    assert 'capture.start' not in [message['type'] for message in list(h.ws.sent._queue)]


@pytest.mark.asyncio
@pytest.mark.parametrize('invalid', [None, 'concealed_samples', 'plc_samples', 'lost_groups', 'late_groups', 'partial'])
async def test_capture_recovery_counters_are_bounded_before_stt_commit(harness, invalid):
    h = harness
    await h.watch('capture.started', {**ID, 'first_group': '0'})
    await h.watch('capture.stopped', {**ID, 'first_group': '0', 'end_group': '3', 'samples': '320'})
    await h.native('capture.pcm', ID, b'\0\0' * 320)
    counters = dict(concealed_samples='320', plc_samples='320', lost_groups='1', late_groups='0')
    if invalid == 'partial': counters.pop('late_groups')
    elif invalid: counters[invalid] = '999999'
    fields = {**ID, 'first_group': '0', 'end_group': '3', 'samples': '320', **counters}
    if invalid:
        with pytest.raises(MoqSessionError): await h.native('capture.ended', fields)
        assert not h.session._capture.validated.is_set()
    else:
        await h.native('capture.ended', fields)
        assert h.session._capture.validated.is_set()


@pytest.mark.asyncio
@pytest.mark.parametrize('pts', [None, True, 0, '-1', '01', str(2**62)])
async def test_playback_timeline_requires_canonical_bounded_native_timestamp(harness, pts):
    h = harness
    await h.capture()
    h.session.begin_downlink()
    h.session.enqueue_downlink(b'\x04\x00' * 537, 16000)
    h.session.end_downlink()
    await h.next_ipc('playback.begin')
    fields = {**ID, 'response_id': '1', 'first_group': '0'}
    if pts is not None:
        fields['pts_us'] = pts
    with pytest.raises(MoqSessionError):
        await h.native('playback.prepared', fields)
    assert not h.session._response.prepared.is_set()
    assert 'playback.begin' not in [item['type'] for item in list(h.ws.sent._queue)]
