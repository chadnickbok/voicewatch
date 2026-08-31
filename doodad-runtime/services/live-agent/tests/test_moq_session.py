"""Session ordering/ownership tests; synthetic audio and public fixture keys only."""
import asyncio
import json

import pytest
import pytest_asyncio

from doodad_agent.metrics import LatencyTrace
from doodad_agent.moq_auth import GrantRegistry, bootstrap_proof
from doodad_agent.moq_ipc import Packet
from doodad_agent.moq_session import MoqSession, MoqSessionError
from doodad_agent.session import DownlinkUtteranceBinding

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
        await self.native('playback.prepared', {**ID, 'response_id': number, 'first_group': first})
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
    await h.watch('hello', dict(device_id=DEVICE, board='t-watch-ultra', transport='moq', capabilities={}))
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
    await h.prepare()
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
    await h.session.resume_after_downlink()
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
