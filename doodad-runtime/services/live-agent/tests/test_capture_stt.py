"""Synthetic provider events exercise the real pinned STT/frame boundaries."""
import asyncio
import base64
import json
from types import SimpleNamespace

import pytest
from pipecat.frames.frames import (
    InputAudioRawFrame, InterimTranscriptionFrame, TranscriptionFrame,
    VADUserStartedSpeakingFrame, VADUserStoppedSpeakingFrame,
)
from pipecat.processors.frame_processor import FrameDirection, FrameProcessor

from doodad_agent.capture_stt import CaptureAudioResampler, CaptureRealtimeSTTService, CaptureTurn, frame_turn
from doodad_agent.conversation import FocusRouter, LiveConversation
from doodad_agent.metrics import LatencyTrace


D = FrameDirection.DOWNSTREAM


async def nothing(*_):
    pass


class Socket:
    def __init__(self):
        self.sent = []
        self.closed = []
        self.committing = asyncio.Event()

    async def send(self, raw):
        message = json.loads(raw)
        self.sent.append(message)
        if message['type'] == 'input_audio_buffer.commit':
            self.committing.set()

    async def close(self, **kwargs):
        self.closed.append(kwargs)


@pytest.fixture
def harness(monkeypatch):
    emitted, failures = [], []
    async def push(processor, frame, direction=D):
        emitted.append(frame)
    async def failed(turn):
        failures.append(turn)
    monkeypatch.setattr(FrameProcessor, 'push_frame', push)
    stt = CaptureRealtimeSTTService(api_key='test-only', on_capture_failure=failed,
                                   acknowledgement_timeout=.2)
    stt._sample_rate = 16000
    stt._session_ready = True
    stt._configured.set()
    stt._websocket = socket = Socket()
    return stt, socket, emitted, failures


async def begin(stt, turn):
    await stt.process_frame(turn.stamp(VADUserStartedSpeakingFrame(start_secs=0)), D)
    await stt.process_frame(turn.stamp(InputAudioRawFrame(b'\1\0'*320, 16000, 1)), D)


async def committing(stt, socket, turn):
    socket.committing.clear()
    task = asyncio.create_task(stt.process_frame(turn.stamp(VADUserStoppedSpeakingFrame(stop_secs=0)), D))
    await asyncio.wait_for(socket.committing.wait(), 1)
    return task


async def ack(stt, item, previous=None):
    await stt._handle_audio_committed({'type':'input_audio_buffer.committed',
                                     'item_id':item, 'previous_item_id':previous})


async def final(stt, item, **extra):
    await stt._handle_transcription_completed({'item_id':item, 'content_index':0,
                                             'transcript':'synthetic test', **extra})


def transcripts(frames):
    return [f for f in frames if isinstance(f, (TranscriptionFrame, InterimTranscriptionFrame))]


@pytest.mark.asyncio
async def test_clear_audio_commit_ack_and_provider_final_keep_original_capture(harness):
    stt, socket, frames, failures = harness
    turn = CaptureTurn()
    await begin(stt, turn)
    end = await committing(stt, socket, turn)
    assert not end.done()
    await final(stt, 'unacknowledged')
    assert not transcripts(frames)
    await ack(stt, 'first')
    await end
    await stt._handle_transcription_delta({'item_id':'first', 'content_index':0, 'delta':'synthetic'})
    await final(stt, 'first')
    await final(stt, 'first')  # Duplicate completion cannot initiate a second turn.
    assert len(transcripts(frames)) == 2
    assert all(frame_turn(f) is turn for f in transcripts(frames))
    assert [m['type'] for m in socket.sent] == ['input_audio_buffer.clear',
        'input_audio_buffer.append', 'input_audio_buffer.commit']
    assert not failures and not socket.closed


@pytest.mark.asyncio
@pytest.mark.parametrize('cancel_before_ack', [True, False])
async def test_old_ack_and_reordered_finals_cannot_be_rebound_to_replacement(harness, cancel_before_ack):
    stt, socket, frames, failures = harness
    old, new = CaptureTurn(), CaptureTurn()
    await begin(stt, old)
    end = await committing(stt, socket, old)
    if cancel_before_ack:
        old.live = False
    await ack(stt, 'old')
    await end
    old.live = False
    await begin(stt, new)
    end = await committing(stt, socket, new)
    await final(stt, 'old')
    await ack(stt, 'new', 'old')
    await end
    await final(stt, 'new')
    await final(stt, 'old')
    assert len(transcripts(frames)) == 1
    assert frame_turn(transcripts(frames)[0]) is new
    assert not socket.closed and not failures


@pytest.mark.asyncio
async def test_cancelled_queued_start_audio_and_end_never_reach_provider(harness):
    stt, socket, frames, _ = harness
    old = CaptureTurn(live=False)
    await begin(stt, old)
    await stt.process_frame(old.stamp(VADUserStoppedSpeakingFrame(stop_secs=0)), D)
    assert not socket.sent and not frames


@pytest.mark.asyncio
async def test_cancel_during_resampling_prevents_late_audio_append(harness):
    stt, socket, _, _ = harness
    entered, release = asyncio.Event(), asyncio.Event()
    async def resample(*_):
        entered.set()
        await release.wait()
        return b'\1\0'*480
    turn = CaptureTurn()
    await stt.process_frame(turn.stamp(VADUserStartedSpeakingFrame(start_secs=0)),D)
    stt._resampler = SimpleNamespace(resample=resample)
    task = asyncio.create_task(stt.process_frame(turn.stamp(InputAudioRawFrame(b'\1\0'*320,16000,1)),D))
    await entered.wait()
    turn.live = False
    release.set()
    await task
    assert [m['type'] for m in socket.sent] == ['input_audio_buffer.clear']


@pytest.mark.asyncio
@pytest.mark.parametrize('event', [
    {}, {'item_id':''}, {'item_id':'x'*257}, {'item_id':'first','previous_item_id':'unknown'},
])
async def test_bad_acknowledgement_retires_capture_and_socket(harness, event):
    stt, socket, frames, failures = harness
    turn = CaptureTurn()
    await begin(stt, turn)
    end = await committing(stt, socket, turn)
    await stt._handle_audio_committed(event)
    await end
    await final(stt, event.get('item_id'))
    assert not turn.live and failures == [turn] and socket.closed
    assert not transcripts(frames)


@pytest.mark.asyncio
async def test_missing_ack_timeout_does_not_assign_late_ack_to_next_capture(harness):
    stt, socket, frames, failures = harness
    stt._acknowledgement_timeout = .01
    old = CaptureTurn()
    await begin(stt, old)
    end = await committing(stt, socket, old)
    await end
    assert not old.live and socket.closed and failures == [old]
    await ack(stt, 'late')
    await final(stt, 'late')
    assert not transcripts(frames)


@pytest.mark.asyncio
async def test_failed_send_cannot_leave_an_unbound_live_capture(harness):
    stt, socket, frames, failures = harness
    async def fail(_):
        raise OSError('synthetic failure')
    socket.send = fail
    turn = CaptureTurn()
    await begin(stt, turn)
    assert not turn.live and failures == [turn] and socket.closed and not frames


@pytest.mark.asyncio
async def test_wrong_item_content_and_stale_provider_errors_are_ignored(harness):
    stt, socket, frames, failures = harness
    turn = CaptureTurn()
    await begin(stt, turn)
    end = await committing(stt, socket, turn)
    await ack(stt, 'current'); await end
    await final(stt, 'current', content_index=1)
    await final(stt, 'other')
    await stt._handle_transcription_failed({'item_id':'old', 'content_index':0})
    assert not transcripts(frames) and not failures
    await final(stt, 'current')
    assert len(transcripts(frames)) == 1


def conversation():
    c = LiveConversation(None,None,None,LatencyTrace(),lambda *_:0,
                         nothing,nothing,nothing,nothing,nothing,nothing,explicit_capture=True)
    c.worker = SimpleNamespace(queue_frame=nothing)
    c._set_voice_phase = nothing
    return c


@pytest.mark.asyncio
async def test_transcript_already_queued_before_cancel_cannot_route_after_new_capture():
    c = conversation()
    await c.capture_started()
    old = c._capture_turn
    await c.capture_completed()
    queued = old.stamp(TranscriptionFrame('synthetic abandoned capture','test',''))
    await c.cancel()
    await c.capture_started()
    routed = []
    controller = SimpleNamespace(route_focused=lambda *_:(routed.append(1) or SimpleNamespace(handled=False)))
    router = FocusRouter(controller,LatencyTrace(),nothing,lambda:not c._retired,c._transcript_current)
    router.push_frame = nothing
    await router.process_frame(queued,D)
    assert not routed
    current = c._capture_turn.stamp(TranscriptionFrame('synthetic current capture','test',''))
    await router.process_frame(current,D)
    assert routed == [1]


@pytest.mark.asyncio
@pytest.mark.parametrize('frame_type', [TranscriptionFrame, InterimTranscriptionFrame])
async def test_cancel_during_transcript_callback_prevents_focus_mutation(frame_type):
    c = conversation()
    await c.capture_started()
    frame = c._capture_turn.stamp(frame_type('synthetic','test',''))
    async def callback(*_):
        await c.cancel()
        await c.capture_started()
    controller = SimpleNamespace(route_focused=lambda *_:pytest.fail('stale focus mutation'))
    router = FocusRouter(controller,LatencyTrace(),callback,lambda:not c._retired,c._transcript_current)
    forwarded = []
    async def push(*args): forwarded.append(args)
    router.push_frame = push
    await router.process_frame(frame,D)
    assert not forwarded


@pytest.mark.asyncio
async def test_new_listen_intent_invalidates_old_result_before_capture_receipt():
    c = conversation()
    await c.capture_started()
    old = c._capture_turn
    await c.capture_completed()
    await c.begin_listening()
    assert not old.live and not c._capture_open


@pytest.mark.asyncio
async def test_old_transcript_stop_receipt_cannot_change_replacement_voice_phase():
    c = conversation()
    phases = []
    async def phase(value): phases.append(value)
    c._set_voice_phase = phase
    async def stop():
        await c.cancel()
        await c.begin_listening()
        await c.capture_started()
    c.stop_capture = stop
    await c.capture_started()
    await c._on_transcript('synthetic', True)
    assert phases == ['ready', 'listening']


@pytest.mark.asyncio
async def test_provider_socket_loss_invalidates_existing_capture_without_audio_replay(harness):
    stt, socket, frames, failures = harness
    turn = CaptureTurn()
    await begin(stt, turn)
    class EndedSocket(Socket):
        def __aiter__(self):
            return self
        async def __anext__(self):
            raise StopAsyncIteration
    stt._websocket = EndedSocket()
    await stt._receive_messages()
    assert not turn.live and failures == [turn]
    assert not stt._configured.is_set()


@pytest.mark.asyncio
async def test_app_text_identity_survives_stt_passthrough_but_cancelled_text_does_not(harness):
    stt, _, frames, _ = harness
    turn = CaptureTurn()
    frame = turn.stamp(TranscriptionFrame('synthetic app text','watch-text',''))
    await stt.process_frame(frame,D)
    turn.live = False
    await stt.process_frame(frame,D)
    assert transcripts(frames) == [frame]


@pytest.mark.asyncio
async def test_resampler_tail_is_flushed_before_commit_and_cancelled_history_is_discarded(harness):
    import numpy as np
    stt, socket, frames, failures = harness
    old, new = CaptureTurn(), CaptureTurn()
    await stt.process_frame(old.stamp(VADUserStartedSpeakingFrame(start_secs=0)),D)
    await stt.process_frame(old.stamp(InputAudioRawFrame(b'\xe0\x2e'*320,16000,1)),D)
    old.live = False
    await stt.process_frame(new.stamp(VADUserStartedSpeakingFrame(start_secs=0)),D)
    start = len(socket.sent)
    await stt.process_frame(new.stamp(InputAudioRawFrame(b'\0\0'*640,16000,1)),D)
    end = await committing(stt,socket,new)
    await ack(stt,'silence'); await end
    pcm = b''.join(base64.b64decode(m['audio']) for m in socket.sent[start:] if 'audio' in m)
    assert len(pcm)//2 == 960  # Exact 16k -> 24k duration including startup tail.
    # libsoxr's int16 conversion dithers; a one-LSB floor is not old speech.
    assert abs(np.frombuffer(pcm,dtype='<i2')).max() <= 1
    assert not failures


@pytest.mark.asyncio
async def test_resampler_small_chunks_match_one_capture_without_timing_resets():
    import numpy as np
    source = np.random.default_rng(41).integers(-12000,12000,3218,dtype=np.int16).tobytes()
    whole, chunked = CaptureAudioResampler(), CaptureAudioResampler()
    expected = await whole.resample(source,16000,24000) + whole.flush()
    actual = b''
    for offset in range(0,len(source),320):
        actual += await chunked.resample(source[offset:offset+320],16000,24000)
    actual += chunked.flush()
    assert len(actual)//2 == 4827
    delta = np.frombuffer(actual,dtype='<i2').astype(int)-np.frombuffer(expected,dtype='<i2').astype(int)
    assert abs(delta).max() <= 2  # Independent int16 dither, exact duration.


@pytest.mark.asyncio
async def test_control_cancellation_fences_stt_before_waiting_for_device_stop():
    from doodad_agent.main import cancel_capture_to_conversation
    c = conversation()
    await c.capture_started()
    frame = c._capture_turn.stamp(TranscriptionFrame('synthetic old capture','test',''))
    calls = []
    async def stop():
        assert not c._capture_open and not c._transcript_current(frame)
        calls.append('stop')
        # A late validated end during stop must not commit the cancelled turn.
        await c.capture_completed()
    await cancel_capture_to_conversation(c,SimpleNamespace(explicit_capture_completion=True,stop_capture=stop))
    assert calls == ['stop']


@pytest.mark.asyncio
async def test_legacy_control_cancellation_keeps_existing_order():
    from doodad_agent.main import cancel_capture_to_conversation
    calls = []
    async def stop(): calls.append('stop')
    async def cancel(): calls.append('cancel')
    await cancel_capture_to_conversation(SimpleNamespace(cancel=cancel),SimpleNamespace(stop_capture=stop))
    assert calls == ['stop','cancel']


@pytest.mark.asyncio
async def test_failure_cleanup_closes_originating_socket_if_callback_replaces_it(harness):
    stt, old, _, _ = harness
    new = Socket()
    async def replace(_): stt._websocket = new
    stt._on_capture_failure = replace
    await begin(stt,CaptureTurn())
    await stt._break_capture()
    assert old.closed and not new.closed


@pytest.mark.asyncio
async def test_configuration_timeout_retires_capture_without_sending_pcm(harness):
    stt, socket, frames, failures = harness
    stt._configured.clear()
    stt._acknowledgement_timeout = .01
    turn = CaptureTurn()
    await begin(stt,turn)
    assert not turn.live and failures == [turn]
    assert not socket.sent and socket.closed and not frames


@pytest.mark.asyncio
async def test_receive_dispatch_reconnect_accepts_only_a_fresh_explicit_capture(harness):
    stt, old_socket, frames, failures = harness
    old = CaptureTurn()
    await begin(stt,old)
    end = await committing(stt,old_socket,old)
    await ack(stt,'old'); await end
    await stt._break_capture()
    class ReceivingSocket(Socket):
        def __init__(self):
            super().__init__()
            self.incoming = asyncio.Queue()
        def __aiter__(self): return self
        async def __anext__(self):
            return json.dumps(await self.incoming.get())
    stt._websocket = new_socket = ReceivingSocket()
    receiving = asyncio.create_task(stt._receive_messages())
    try:
        await new_socket.incoming.put({'type':'session.updated'})
        await asyncio.wait_for(stt._configured.wait(),1)
        # Old queued PCM/end must not be replayed even though the new socket is ready.
        await stt.process_frame(old.stamp(InputAudioRawFrame(b'\1\0'*320,16000,1)),D)
        await stt.process_frame(old.stamp(VADUserStoppedSpeakingFrame(stop_secs=0)),D)
        assert not new_socket.sent
        current = CaptureTurn()
        await begin(stt,current)
        end = await committing(stt,new_socket,current)
        await new_socket.incoming.put({'type':'input_audio_buffer.committed','item_id':'new','previous_item_id':None})
        await asyncio.wait_for(end,1)
        for item in ('old','new'):
            await new_socket.incoming.put({'type':'conversation.item.input_audio_transcription.completed',
                'item_id':item,'content_index':0,'transcript':'synthetic'})
        async with asyncio.timeout(1):
            while not transcripts(frames): await asyncio.sleep(0)
        assert len(transcripts(frames)) == 1 and frame_turn(transcripts(frames)[0]) is current
        assert failures == [old]
    finally:
        receiving.cancel()
        await asyncio.gather(receiving,return_exceptions=True)
