#!/usr/bin/env python3
"""Real provider bench: host-initiated capture, physical Ultra mic and speaker.

Runs the actual serve/conversation path with an isolated database and temporary
PKI. The Mac speaks a fixed test phrase near the watch. Only read-only model
tools are exposed; no jobs, email, personal app delivery or data mutation. No
ambient PCM is retained. All raw process/provider logs must be redirected to a
private file by the caller. Requires provider keys in the environment.
"""
import argparse
import asyncio
import copy
import json
import os
import re
from pathlib import Path
import secrets
import socket
import struct
import time
import wave

from moq_ultra_bench import ROOT, ENROLL, ENDPOINT, pki, port, stop, write


async def run(args):
    from doodad_agent import conversation as conversation_module, main, transport_moq
    from doodad_agent.conversation import LiveConversation
    from doodad_agent.capture_stt import frame_turn
    from pipecat.frames.frames import TTSStartedFrame, TTSAudioRawFrame

    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.umask(0o077)
    # Never inherit personal signing/deployment or job storage into the bench.
    for key in ('DOODAD_PERSONAL_OWNER_ID', 'DOODAD_PERSONAL_HMAC_KEY_HEX', 'DOODAD_PERSONAL_SIGNER_KEY_ID'):
        os.environ.pop(key, None)
    os.environ['DOODAD_CODEX_WORKSPACE_ROOT'] = str(output/'jobs')
    result = dict(pass_=False, provider_calls=True, control_source='host-test-driver',
                  microphone_samples=0, firmware_written=False, restoration_required=False,
                  capture_rounds_requested=args.capture_rounds, turns=[])
    start = time.monotonic()
    microphone_square_sum = 0
    level_samples = level_square_sum = level_peak = level_clipped = 0
    result['microphone_peak'] = 0
    saved_output = None
    acoustic_pcm = bytearray()
    fixture_pcm = b''
    held_stt_final = None
    hold_next_final = args.cancel_first_stt
    stt_final_held = asyncio.Event()
    held_tool_result = None
    hold_next_tool = args.cancel_first_tool
    tool_result_held = asyncio.Event()
    held_tts_frames = []
    held_tts_turn = None
    hold_next_tts = args.cancel_first_tts
    tts_audio_held = asyncio.Event()
    conversations = []
    children, streams = [], []
    server_task = None
    ready = asyncio.Queue()
    captured = asyncio.Event()
    server_started = asyncio.get_running_loop().create_future()
    real_transport, real_tools = transport_moq.MoqTransportServer, LiveConversation._tools
    real_stt = conversation_module.CaptureRealtimeSTTService
    real_tts = conversation_module.CaptureElevenLabsTTSService
    real_current_tool = LiveConversation._current_tool
    result.update(stt_samples_submitted=0, stt_commits=0, stt_completed_events=0,
                  stt_completed_characters=0, stt_error_count=0, fixture_recognized=False)

    def mark(kind, **values):
        event = dict(kind=kind, elapsed_ms=round((time.monotonic()-start)*1000), **values)
        with (output/'events.jsonl').open('a') as stream:
            stream.write(json.dumps(event)+'\n')

    class ObservedSTT(real_stt):
        def __init__(self, *values, **kwargs):
            if args.stt_noise_reduction is not None:
                kwargs['settings'].noise_reduction = (
                    None if args.stt_noise_reduction == 'off' else args.stt_noise_reduction)
                result['stt_noise_reduction_override'] = args.stt_noise_reduction
            super().__init__(*values, **kwargs)

        async def _handle_session_updated(self, event):
            settings = event.get('session', {}).get('audio', {}).get('input', {})
            reduction = settings.get('noise_reduction', 'unreported')
            effective = reduction.get('type') if isinstance(reduction, dict) else reduction
            result['stt_effective_noise_reduction'] = (
                effective if effective is None or
                (isinstance(effective, str) and effective in {'near_field', 'far_field'}) else 'unreported')
            await super()._handle_session_updated(event)

        async def _send_audio(self, audio):
            await super()._send_audio(audio)
            result['stt_samples_submitted'] += len(audio)//2

        async def _commit_audio_buffer(self):
            await super()._commit_audio_buffer()
            result['stt_commits'] += 1
            mark('stt_commit_sent')

        async def _handle_transcription_completed(self, event):
            nonlocal held_stt_final, hold_next_final
            if hold_next_final and self._event_capture(event) is not None:
                # Delay one real provider event in memory, without injecting a
                # transcript or persisting its content. Release it only after
                # cancellation and a replacement capture have been observed.
                hold_next_final = False
                held_stt_final = (self, event)
                stt_final_held.set()
                mark('stt_final_deliberately_delayed')
                return
            if self._event_capture(event) is None:
                result['stt_rejected_completions'] = result.get('stt_rejected_completions', 0) + 1
                await super()._handle_transcription_completed(event)
                return
            result['stt_completed_events'] += 1
            result['stt_completed_characters'] += len(event.get('transcript', ''))
            result['fixture_recognized'] |= bool(re.search(r'\b(exercise|workout|set)\b',
                event.get('transcript', ''), re.IGNORECASE))
            result['fixture_keyword_matches'] = {word: bool(re.search(r'\b'+word+r'\b',
                event.get('transcript', ''), re.IGNORECASE))
                for word in ('please', 'read', 'my', 'next', 'exercise', 'set')}
            mark('stt_completion_received', characters=len(event.get('transcript', '')))
            await super()._handle_transcription_completed(event)

        async def _handle_error(self, event):
            result['stt_error_count'] += 1
            mark('stt_provider_error')
            await super()._handle_error(event)

    def observed_current_tool(conversation, handler):
        async def observed_handler(params):
            original = params.result_callback
            async def deliver(*values, **kwargs):
                nonlocal held_tool_result, hold_next_tool
                if hold_next_tool:
                    hold_next_tool = False
                    held_tool_result = (original, values, kwargs)
                    tool_result_held.set()
                    mark('real_tool_result_deliberately_delayed')
                    return
                await original(*values, **kwargs)
            delayed = copy.copy(params)
            delayed.result_callback = deliver
            await handler(delayed)
        return real_current_tool(conversation, observed_handler)

    class ObservedTTS(real_tts):
        async def push_frame(self, frame, direction=conversation_module.FrameDirection.DOWNSTREAM):
            nonlocal held_tts_turn, hold_next_tts
            turn = frame_turn(frame) or self._capture_contexts.get(getattr(frame, 'context_id', None)) or self._origin.get()
            if hold_next_tts and isinstance(frame, TTSStartedFrame):
                hold_next_tts = False
                held_tts_turn = turn
                if turn is None: raise RuntimeError('unbound physical TTS start')
                turn.stamp(frame)
                held_tts_frames.append((self, frame, direction))
                return
            if held_tts_turn is not None and turn is held_tts_turn:
                if isinstance(frame, TTSAudioRawFrame) and not tts_audio_held.is_set():
                    # Retain exactly one real provider audio frame, never mic PCM.
                    if len(frame.audio) > 1_048_576: raise RuntimeError('TTS fault frame bound')
                    turn.stamp(frame)
                    held_tts_frames.append((self, frame, direction))
                    tts_audio_held.set()
                    mark('real_tts_audio_deliberately_delayed')
                return
            await super().push_frame(frame, direction)

    class ObservedTransport(real_transport):
        def __init__(self, trace, on_audio, on_event, *more, **kwargs):
            async def audio(device_id, pcm):
                nonlocal microphone_square_sum, level_samples, level_square_sum, level_peak, level_clipped
                if args.acoustic_analysis:
                    if len(acoustic_pcm) + len(pcm) > 31 * 16000 * 2:
                        raise RuntimeError('acoustic analysis capture bound')
                    acoustic_pcm.extend(pcm)
                result['microphone_samples'] += len(pcm)//2
                for (sample,) in struct.iter_unpack('<h', pcm):
                    microphone_square_sum += sample*sample
                    result['microphone_peak'] = max(result['microphone_peak'], abs(sample))
                    level_samples += 1
                    level_square_sum += sample*sample
                    level_peak = max(level_peak, abs(sample))
                    level_clipped += abs(sample) >= 32767
                    if level_samples == 8000:
                        mark('microphone_level', samples=level_samples,
                             rms=round((level_square_sum/level_samples)**0.5, 2),
                             peak=level_peak, clipped=level_clipped)
                        level_samples = level_square_sum = level_peak = level_clipped = 0
                await on_audio(device_id, pcm)
            async def event(device_id, kind, payload):
                try:
                    await on_event(device_id, kind, payload)
                except Exception as error:
                    mark('callback_failed', error_type=type(error).__name__)
                    raise
                if kind in ('identified', 'connected', 'disconnected', 'capture.started', 'capture.stopped',
                            'capture.failed', 'listen.requested', 'listen.finished', 'listen.cancelled'):
                    mark(kind)
                if kind == 'connected': ready.put_nowait(self.sessions[device_id])
                if kind == 'capture.started': captured.set()
            super().__init__(trace, audio, event, *more, **kwargs)

        async def start(self):
            await super().start()
            server_started.set_result(self)

    transport_moq.MoqTransportServer = ObservedTransport
    conversation_module.CaptureRealtimeSTTService = ObservedSTT
    conversation_module.CaptureElevenLabsTTSService = ObservedTTS
    LiveConversation._current_tool = observed_current_tool
    def observed_tools(conversation):
        conversations.append(conversation)
        return [tool for tool in real_tools(conversation)
                if tool.name in {'get_next_set', 'get_task_status'}]
    LiveConversation._tools = observed_tools

    def spoken_history_count():
        context = conversations[-1]._context if conversations else None
        return sum(isinstance(message, dict) and message.get('role') == 'assistant'
                   and bool(message.get('content')) for message in context.get_messages()) if context else 0

    async def child(*command, logfile):
        stream = (output/logfile).open('wb'); streams.append(stream)
        process = await asyncio.create_subprocess_exec(*map(str, command), stdout=stream, stderr=asyncio.subprocess.STDOUT)
        children.append(process)
        return process

    async def usb(command, filename, *extra):
        process = await child(args.idf_python, ENROLL, command, '--port', args.port,
                              '--output', output/filename, *extra, logfile='usb-'+command+'.log')
        if await asyncio.wait_for(process.wait(), 15):
            raise RuntimeError('USB command failed')

    async def audio_setting(script):
        process = await asyncio.create_subprocess_exec('/usr/bin/osascript', '-e', script,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        children.append(process)
        stdout, _ = await asyncio.wait_for(process.communicate(), 5)
        if process.returncode:
            raise RuntimeError('fixture audio setting failed')
        return stdout.decode('ascii').strip()

    async def restore_output():
        nonlocal saved_output
        if saved_output is not None:
            volume, muted = saved_output
            await audio_setting(f'set volume output volume {volume} output muted {str(muted).lower()}')
            saved_output = None
            result['fixture_audio_restored'] = True

    try:
        await usb('info', 'device.json')
        device = json.loads((output/'device.json').read_text())
        _, roots = pki(output, args.host)
        key = secrets.token_bytes(32)
        control, clock, media = port(socket.SOCK_STREAM), port(socket.SOCK_STREAM), port(socket.SOCK_DGRAM)
        write(output/'devices.json', {device['device_id']: key.hex()})
        write(output/'host.json', dict(certificate=str(output/'server.pem'), private_key=str(output/'server.key'),
            device_keys=str(output/'devices.json'), ipc_socket=str(output/'media.sock'),
            public_host=args.host, media_port=media, time_port=clock))
        write(output/'endpoint.json', dict(listen=f'0.0.0.0:{media}', certificate=str(output/'server.pem'),
            private_key=str(output/'server.key'), ipc_socket=str(output/'media.sock')))
        arguments = main.parse_arguments(['serve', '--transport', 'moq', '--moq-config', str(output/'host.json'),
            '--port', str(control), '--database', str(output/'bench.sqlite3'), '--trace', str(output/'trace.jsonl')])
        server_task = asyncio.create_task(main.serve(arguments))
        server = await asyncio.wait_for(server_started, 15)
        native = await child(ENDPOINT, '--config', output/'endpoint.json', logfile='native.log')
        write(output/'profile.json', dict(v=1, revision=device['revision']+1, device_id=device['device_id'],
            host=args.host, control_port=control, time_port=clock, roots_pem=roots, key_hex=key.hex()))
        await usb('install', 'installed.json', '--profile', output/'profile.json')
        await child(args.idf_python, ENROLL, 'monitor', '--port', args.port,
                    '--output', output/'serial.log', '--seconds', '240', logfile='monitor.log')
        mark('enrolled')
        session = await asyncio.wait_for(ready.get(), 45)
        # Provider start frames and STT session configuration are asynchronous.
        await asyncio.sleep(3)
        phrase = 'Please read my next exercise set.'
        speech = await child('/usr/bin/say', '-o', output/'input.aiff', phrase, logfile='speech.log')
        if await asyncio.wait_for(speech.wait(), 15): raise RuntimeError('speech fixture failed')
        if args.acoustic_analysis:
            converter = await child('/usr/bin/afconvert', '-f', 'WAVE', '-d', 'LEI16@16000',
                                    '-c', '1', output/'input.aiff', output/'fixture.wav', logfile='convert.log')
            if await asyncio.wait_for(converter.wait(), 15): raise RuntimeError('fixture conversion failed')
            with wave.open(str(output/'fixture.wav'), 'rb') as source:
                if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 16000):
                    raise RuntimeError('fixture PCM format')
                if source.getnframes() > 31 * 16000: raise RuntimeError('fixture PCM bound')
                fixture_pcm = source.readframes(source.getnframes())
        async def provider_turn(round_number, *, cancel_at_stt=False, cancel_at_tool=False, cancel_at_tts=False):
            nonlocal saved_output, held_stt_final, held_tool_result
            captured.clear()
            sample_base = result['microphone_samples']
            event_base = result['stt_completed_events']
            character_base = result['stt_completed_characters']
            previous_response = session._response.number if session._response else 0
            history_base = spoken_history_count()
            trace_base = len((output/'trace.jsonl').read_text().splitlines())
            result['fixture_recognized'] = False
            result['fixture_keyword_matches'] = {}
            await server.on_event(device['device_id'], 'listen.requested', {})
            await asyncio.wait_for(captured.wait(), 5)
            if held_stt_final is not None and not cancel_at_stt:
                observed, event = held_stt_final
                held_stt_final = None
                await observed._handle_transcription_completed(event)
                event.clear()
                if result['stt_completed_events'] != event_base or result.get('stt_rejected_completions') != 1:
                    raise RuntimeError('cancelled STT final crossed capture boundary')
                result['delayed_stt_final_rejected'] = True
                mark('cancelled_stt_final_rejected_during_new_capture')
            if held_tool_result is not None and not cancel_at_tool:
                callback, values, kwargs = held_tool_result
                held_tool_result = None
                try:
                    await callback(*values, **kwargs)
                except ConnectionError:
                    result['delayed_tool_result_rejected'] = True
                    mark('cancelled_tool_result_rejected_during_new_capture')
                else:
                    raise RuntimeError('cancelled tool result crossed capture boundary')
                finally:
                    values = ()
                    kwargs.clear()
            if held_tts_frames and not cancel_at_tts:
                for observed, frame, direction in held_tts_frames:
                    await real_tts.push_frame(observed, frame, direction)
                held_tts_frames.clear()
                await asyncio.sleep(.1)
                trace = [json.loads(line) for line in (output/'trace.jsonl').read_text().splitlines()[trace_base:]]
                if any(item['kind'] in {'tts.started', 'tts.first_audio', 'downlink.first_audio'} for item in trace):
                    raise RuntimeError('cancelled TTS frames crossed capture boundary')
                result['delayed_tts_frames_rejected'] = True
                mark('cancelled_tts_frames_rejected_during_new_capture')
            volume = int(await audio_setting('output volume of (get volume settings)'))
            muted = await audio_setting('output muted of (get volume settings)')
            if not 0 <= volume <= 100 or muted not in {'true', 'false'}:
                raise RuntimeError('invalid fixture audio setting')
            if args.fixture_volume is not None:
                saved_output = (volume, muted == 'true')
                result['fixture_audio_restored'] = False
                await audio_setting(f'set volume output volume {args.fixture_volume} output muted false')
                result['fixture_volume'] = args.fixture_volume
            elif muted == 'true' or volume == 0:
                raise RuntimeError('fixture output is muted; choose --fixture-volume')
            mark('speaking_fixture_to_watch')
            player = await child('/usr/bin/afplay', output/'input.aiff', logfile='afplay.log')
            if await asyncio.wait_for(player.wait(), 20): raise RuntimeError('fixture playback failed')
            await restore_output()
            await asyncio.sleep(.5)
            await server.on_event(device['device_id'], 'listen.finished', {})
            if cancel_at_stt:
                await asyncio.wait_for(stt_final_held.wait(), 30)
                await server.on_event(device['device_id'], 'listen.cancelled', {})
                trace = [json.loads(line) for line in (output/'trace.jsonl').read_text().splitlines()[trace_base:]]
                if any(item['kind'] in {'stt.final', 'tool.start', 'tool.end', 'tts.first_audio'} for item in trace):
                    raise RuntimeError('cancelled provider turn reached a downstream stage')
                result['cancelled_capture_samples'] = result['microphone_samples'] - sample_base
                mark('provider_capture_cancelled_before_stt_delivery', microphone_samples=result['cancelled_capture_samples'])
                return
            if cancel_at_tool or cancel_at_tts:
                held = tool_result_held if cancel_at_tool else tts_audio_held
                await asyncio.wait_for(held.wait(), 45)
                await server.on_event(device['device_id'], 'listen.cancelled', {})
                trace = [json.loads(line) for line in (output/'trace.jsonl').read_text().splitlines()[trace_base:]]
                if any(item['kind'] in {'tts.first_audio', 'downlink.first_audio'} for item in trace):
                    raise RuntimeError('faulted turn escaped held provider output')
                if spoken_history_count() != history_base:
                    raise RuntimeError('unplayed faulted speech entered assistant history')
                result['cancelled_capture_samples'] = result['microphone_samples'] - sample_base
                mark('provider_capture_cancelled_after_tool' if cancel_at_tool else 'provider_capture_cancelled_after_tts',
                     microphone_samples=result['cancelled_capture_samples'])
                return
            async with asyncio.timeout(90):
                while True:
                    if session._closed: raise RuntimeError('session closed during provider turn')
                    if result['stt_completed_events'] > event_base and result['stt_completed_characters'] == character_base:
                        raise RuntimeError('spoken fixture produced an empty final transcript')
                    response = session._response
                    if (response is not None and response.number > previous_response
                            and response.finished.is_set() and response.done.is_set()
                            and spoken_history_count() > history_base):
                        result['speaker_samples'] = response.samples
                        result['response_id'] = response.number
                        break
                    await asyncio.sleep(.1)
            trace = [json.loads(line) for line in (output/'trace.jsonl').read_text().splitlines()[trace_base:]]
            result['stt_final'] = any(item['kind']=='stt.final' for item in trace)
            result['tts_audio'] = any(item['kind']=='tts.first_audio' for item in trace)
            result['read_tool_completed'] = any(item['kind']=='tool.end' and item.get('tool')=='get_next_set' for item in trace)
            if (result['microphone_samples'] <= sample_base or
                result['stt_completed_events'] <= event_base or
                not all(result[key] for key in ('stt_final', 'fixture_recognized', 'tts_audio', 'read_tool_completed', 'speaker_samples'))):
                raise RuntimeError('provider turn did not satisfy all required stages')
            turn = dict(round=round_number, microphone_samples=result['microphone_samples']-sample_base,
                        stt_characters=result['stt_completed_characters']-character_base,
                        response_id=result['response_id'], speaker_samples=result['speaker_samples'],
                        fixture_recognized=result['fixture_recognized'], read_tool_completed=result['read_tool_completed'])
            turn['spoken_history_messages_added'] = spoken_history_count() - history_base
            result['turns'].append(turn)
            mark('provider_turn_completed', **turn)
        if args.cancel_first_stt:
            await provider_turn(0, cancel_at_stt=True)
        if args.cancel_first_tool:
            await provider_turn(0, cancel_at_tool=True)
        if args.cancel_first_tts:
            await provider_turn(0, cancel_at_tts=True)
        for round_number in range(1, args.capture_rounds + 1):
            await provider_turn(round_number)
        if args.cancel_first_stt and not result.get('delayed_stt_final_rejected'):
            raise RuntimeError('missing STT cancellation evidence')
        if args.cancel_first_tool and not result.get('delayed_tool_result_rejected'):
            raise RuntimeError('missing tool cancellation evidence')
        if args.cancel_first_tts and not result.get('delayed_tts_frames_rejected'):
            raise RuntimeError('missing TTS cancellation evidence')
        old_id = session.session_id
        await session.close(code=4000, message=b'provider bench replacement')
        replacement = await asyncio.wait_for(ready.get(), 25)
        if replacement.session_id == old_id: raise RuntimeError('session grant reused')
        before = result['microphone_samples']
        await asyncio.sleep(3)
        if result['microphone_samples'] != before: raise RuntimeError('reconnect started microphone')
        result['provider_pipeline_reconnected'] = True
        result['pass_'] = True
        mark('passed')
    except Exception as error:
        result['failure_type'] = type(error).__name__
        mark('failed', error_type=type(error).__name__)
        raise
    finally:
        try:
            await restore_output()
        except Exception:
            result['fixture_audio_restored'] = False
            result['pass_'] = False
            mark('fixture_audio_restore_failed')
        if server_task is not None:
            server_task.cancel()
            try:
                await asyncio.wait_for(asyncio.gather(server_task, return_exceptions=True), 25)
            except TimeoutError:
                result['shutdown_timeout'] = True
        for process in reversed(children): await stop(process)
        for stream in streams: stream.close()
        try:
            if args.acoustic_analysis and acoustic_pcm and fixture_pcm:
                from moq_acoustic_analysis import analyze
                write(output/'acoustic-analysis.json', analyze(acoustic_pcm, fixture_pcm))
        except Exception as error:
            result['acoustic_analysis_failure_type'] = type(error).__name__
            result['pass_'] = False
        finally:
            acoustic_pcm[:] = b'\0' * len(acoustic_pcm)
            acoustic_pcm.clear()
            if held_stt_final is not None:
                held_stt_final[1].clear()
                held_stt_final = None
            held_tool_result = None
            held_tts_frames.clear()
        transport_moq.MoqTransportServer, LiveConversation._tools = real_transport, real_tools
        LiveConversation._current_tool = real_current_tool
        conversation_module.CaptureRealtimeSTTService = real_stt
        conversation_module.CaptureElevenLabsTTSService = real_tts
        result['microphone_rms'] = round((microphone_square_sum/max(1, result['microphone_samples']))**0.5, 2)
        result['elapsed_ms'] = round((time.monotonic()-start)*1000)
        write(output/'result.json', result)
    if not result['pass_']:
        raise RuntimeError('provider bench final verification failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--idf-python', type=Path, required=True)
    parser.add_argument('--fixture-volume', type=int, help='Temporarily set Mac output volume for the spoken fixture, then restore it')
    parser.add_argument('--acoustic-analysis', action='store_true',
                        help='Compare bounded microphone PCM in memory with the synthetic fixture; save only numeric diagnostics')
    parser.add_argument('--stt-noise-reduction', choices=('near_field', 'far_field', 'off'),
                        help='Bench-only provider noise reduction experiment; does not change production defaults')
    parser.add_argument('--capture-rounds', type=int, default=1,
                        help='Repeat 1..3 complete provider turns in the same authenticated session')
    faults = parser.add_mutually_exclusive_group()
    faults.add_argument('--cancel-first-stt', action='store_true',
                        help='First cancel a physical capture with a delayed real STT final; release that event during the next capture')
    faults.add_argument('--cancel-first-tool', action='store_true',
                        help='Hold a real read-tool result; cancel and release its callback during the next capture')
    faults.add_argument('--cancel-first-tts', action='store_true',
                        help='Hold real TTS start/audio frames; cancel and release them during the next capture')
    args = parser.parse_args()
    if not 1 <= args.capture_rounds <= 3:
        parser.error('capture-rounds must be 1..3')
    if args.fixture_volume is not None and not 1 <= args.fixture_volume <= 100:
        parser.error('fixture-volume must be 1..100')
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit('Provider bench failed ('+type(error).__name__+'); inspect private evidence') from None


if __name__ == '__main__': main()
