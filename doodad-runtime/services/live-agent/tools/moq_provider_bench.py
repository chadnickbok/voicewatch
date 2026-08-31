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
import json
import os
from pathlib import Path
import secrets
import socket
import time

from moq_ultra_bench import ROOT, ENROLL, ENDPOINT, pki, port, stop, write


async def run(args):
    from doodad_agent import main, transport_moq
    from doodad_agent.conversation import LiveConversation

    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    os.umask(0o077)
    # Never inherit personal signing/deployment or job storage into the bench.
    for key in ('DOODAD_PERSONAL_OWNER_ID', 'DOODAD_PERSONAL_HMAC_KEY_HEX', 'DOODAD_PERSONAL_SIGNER_KEY_ID'):
        os.environ.pop(key, None)
    os.environ['DOODAD_CODEX_WORKSPACE_ROOT'] = str(output/'jobs')
    result = dict(pass_=False, provider_calls=True, control_source='host-test-driver',
                  microphone_samples=0, firmware_written=False, restoration_required=False)
    start = time.monotonic()
    children, streams = [], []
    server_task = None
    ready = asyncio.Queue()
    captured = asyncio.Event()
    server_started = asyncio.get_running_loop().create_future()
    real_transport, real_tools = transport_moq.MoqTransportServer, LiveConversation._tools

    def mark(kind, **values):
        event = dict(kind=kind, elapsed_ms=round((time.monotonic()-start)*1000), **values)
        with (output/'events.jsonl').open('a') as stream:
            stream.write(json.dumps(event)+'\n')

    class ObservedTransport(real_transport):
        def __init__(self, trace, on_audio, on_event, *more, **kwargs):
            async def audio(device_id, pcm):
                result['microphone_samples'] += len(pcm)//2
                await on_audio(device_id, pcm)
            async def event(device_id, kind, payload):
                try:
                    await on_event(device_id, kind, payload)
                except Exception as error:
                    mark('callback_failed', error_type=type(error).__name__)
                    raise
                if kind in ('identified', 'connected', 'disconnected', 'capture.started', 'capture.stopped'):
                    mark(kind)
                if kind == 'connected': ready.put_nowait(self.sessions[device_id])
                if kind == 'capture.started': captured.set()
            super().__init__(trace, audio, event, *more, **kwargs)

        async def start(self):
            await super().start()
            server_started.set_result(self)

    transport_moq.MoqTransportServer = ObservedTransport
    LiveConversation._tools = lambda self: [tool for tool in real_tools(self)
                                            if tool.name in {'get_next_set', 'get_task_status'}]

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
        captured.clear()
        await server.on_event(device['device_id'], 'listen.requested', {})
        await asyncio.wait_for(captured.wait(), 5)
        mark('speaking_fixture_to_watch')
        player = await child('/usr/bin/afplay', output/'input.aiff', logfile='afplay.log')
        if await asyncio.wait_for(player.wait(), 20): raise RuntimeError('fixture playback failed')
        await asyncio.sleep(.5)
        await server.on_event(device['device_id'], 'listen.finished', {})
        async with asyncio.timeout(90):
            while True:
                if session._closed: raise RuntimeError('session closed during provider turn')
                response = session._response
                if response is not None and response.finished.is_set() and response.done.is_set():
                    result['speaker_samples'] = response.samples
                    result['response_id'] = response.number
                    break
                await asyncio.sleep(.1)
        trace = [json.loads(line) for line in (output/'trace.jsonl').read_text().splitlines()]
        result['stt_final'] = any(item['kind']=='stt.final' for item in trace)
        result['tts_audio'] = any(item['kind']=='tts.first_audio' for item in trace)
        result['read_tool_completed'] = any(item['kind']=='tool.end' and item.get('tool')=='get_next_set' for item in trace)
        if not all(result[key] for key in ('stt_final', 'tts_audio', 'read_tool_completed', 'microphone_samples', 'speaker_samples')):
            raise RuntimeError('provider turn did not satisfy all required stages')
        mark('provider_turn_completed', speaker_samples=result['speaker_samples'])
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
        if server_task is not None:
            server_task.cancel()
            try:
                await asyncio.wait_for(asyncio.gather(server_task, return_exceptions=True), 25)
            except TimeoutError:
                result['shutdown_timeout'] = True
        for process in reversed(children): await stop(process)
        for stream in streams: stream.close()
        transport_moq.MoqTransportServer, LiveConversation._tools = real_transport, real_tools
        result['elapsed_ms'] = round((time.monotonic()-start)*1000)
        write(output/'result.json', result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--idf-python', type=Path, required=True)
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit('Provider bench failed ('+type(error).__name__+'); inspect private evidence') from None


if __name__ == '__main__': main()
