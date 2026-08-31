#!/usr/bin/env python3
"""Verify a PRIVATE full-shell diagnostic recording without publishing its PCM.

This gate verifies the media exchange and hardware completion markers. It does
not assert production authentication, Voice Orb/PTT, speech quality or UI parity.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

SAMPLES = 19200


def analyze(serial, expected, microphone, peer_log, result):
    chunks = {}
    for offset, hexdata in re.findall(r'ECHOPCM offset=(\d+) hex=([0-9a-f]+)', serial):
        index = int(offset)
        data = bytes.fromhex(hexdata)
        if index in chunks or len(data) != 320:
            raise ValueError('duplicate or invalid PCM block')
        chunks[index] = data
    if set(chunks) != set(range(0, SAMPLES, 160)):
        raise ValueError('missing or extra PCM block')
    actual = b''.join(chunks[index] for index in range(0, SAMPLES, 160))
    if len(expected) != SAMPLES * 2 or len(microphone) != SAMPLES * 2:
        raise ValueError('incorrect reference sample length')
    errors = [a-b for a, b in zip(struct.unpack('<19200h', actual), struct.unpack('<19200h', expected))]
    rms = math.sqrt(sum(x*x for x in errors) / len(errors))
    comparison = dict(bit_exact=actual == expected, different_samples=sum(x != 0 for x in errors),
                      max_abs_lsb=max(map(abs, errors)), rms_lsb=rms)
    events = []
    pattern = (r'I \((\d+)\) shell-moq-test: event kind=(\d+) session=(\d+) capture=(\d+) '
               r'response=(\d+) samples=(\d+) first=(\d+) end=(\d+) cancelled=(\d+) error=(\d+)')
    for row in re.findall(pattern, serial):
        events.append(dict(zip(('at_ms','kind','session','capture','response','samples','first','end','cancelled','error'), map(int, row))))
    successful = lambda e: e['session'] == 1 and not e['error'] and not e['cancelled']
    captures = [e for e in events if e['kind'] == 3 and successful(e)]
    playbacks = [e for e in events if e['kind'] == 7 and successful(e)]
    checks = {
        'firmware_terminal_pass': 'SHELL_MOQ_FINAL pass=1' in serial and result.get('test_pass') is True,
        'app0_only': result.get('changed_offset') == 65536 and result.get('restoration_required') is False,
        'no_crash_or_failure': not any(x in serial for x in ('FAIL:', 'Guru Meditation', 'abort() was called', 'assert failed')),
        'reference_peer_pass': 'PASS: physical microphone decoded/re-encoded through reference audio; input_samples=19200 response_samples=19200 paced_ms=20' in peer_log,
        'one_capture_with_exact_tail': len(captures) == 1 and all(e['capture'] == 71 and e['samples'] == SAMPLES and e['first'] == 0 and e['end'] == 62 for e in captures),
        'one_owned_drained_response': len(playbacks) == 1 and all(e['capture'] == 71 and e['response'] == 1 and e['samples'] == SAMPLES and e['first'] == 0 and e['end'] == 61 for e in playbacks),
        'codec_tolerance': rms <= 8 and comparison['max_abs_lsb'] <= 1,
        'wamr_running': '[host] app started; instance remains resident' in serial,
        'shell_display_active': '[display]' in serial,
    }
    displays = []
    for line in serial.splitlines():
        if '[display]' in line:
            displays.append({k: int(v) for k, v in re.findall(r'\b(internal_free|internal_min|internal_largest|max_render_us|max_flush_us|total_frames|total_flushes)=(\d+)', line)})
    peak = lambda key: max((d[key] for d in displays if key in d), default=0)
    floor = lambda key: min((d[key] for d in displays if key in d), default=0)
    memory = dict(minimum_internal=floor('internal_min'), minimum_largest_block=floor('internal_largest'),
                  proposed_minimum_internal=96*1024, proposed_largest_block=32*1024)
    memory['diagnostic_meets_proposed_floor'] = memory['minimum_internal'] >= 96*1024 and memory['minimum_largest_block'] >= 32*1024
    return dict(media_exchange_pass=all(checks.values()), checks=checks, pcm_comparison=comparison,
                samples_each_direction=SAMPLES, image_sha256=result.get('image_sha256'), events=events,
                memory=memory, ui=dict(max_observed_render_us=peak('max_render_us'), max_observed_flush_us=peak('max_flush_us')),
                scope='Private reference echo through the full running shell. Public voice control/authentication, actual PTT/Voice Orb, optical/acoustic quality and final acceptance remain unverified.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case_dir', type=Path)
    args = parser.parse_args()
    root = args.case_dir
    sources = [root/'device/serial.log', root/'peer/response.pcm', root/'peer/microphone.pcm', root/'peer.log', root/'device/result.json']
    report = analyze(sources[0].read_text(errors='replace'), sources[1].read_bytes(), sources[2].read_bytes(),
                     sources[3].read_text(), json.loads(sources[4].read_text()))
    report['source_sha256'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (root/'analysis.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    if not report['media_exchange_pass']:
        raise SystemExit('MoQ shell media exchange verification failed')


if __name__ == '__main__':
    main()
