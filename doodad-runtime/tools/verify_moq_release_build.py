#!/usr/bin/env python3
"""Inspect a normal Ultra MoQ build without printing sdkconfig secrets.

This checks compiled feature selection and provenance, not hardware acceptance.
Only --public-ci allows publishing the image; it requires exact public dummy
credentials. Never upload a private product image or its sdkconfig.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def inspect(build: Path, nm: str, public_ci: bool = False) -> dict:
    config = {}
    for line in (build / 'sdkconfig').read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key, value = line.split('=', 1)
            config[key] = value
    required = ('DOODAD_BOARD_TWATCH_ULTRA', 'DOODAD_VOICE_UPLINK',
                'DOODAD_VOICE_TRANSPORT_MOQ', 'DOODAD_PERSONAL_APPS',
                'MBEDTLS_HAVE_TIME_DATE', 'SPIRAM_MODE_QUAD',
                'ESPTOOLPY_FLASHSIZE_16MB')
    for name in required:
        if config.get('CONFIG_' + name) != 'y':
            raise ValueError('required feature missing: ' + name)
    if config.get('CONFIG_DOODAD_VOICE_TRANSPORT_WEBRTC') == 'y':
        raise ValueError('WebRTC selected')
    if public_ci:
        expected = {
            'DOODAD_WIFI_SSID': 'voicewatch-public-build-only',
            'DOODAD_WIFI_PASSWORD': 'not-a-network-password',
            'DOODAD_PERSONAL_OWNER_ID': 'public-build-owner',
            'DOODAD_PERSONAL_SIGNER_KEY_ID': 'public-build-only',
            'DOODAD_PERSONAL_HMAC_KEY_HEX': bytes(range(32)).hex(),
        }
        for key, value in expected.items():
            if config.get('CONFIG_' + key) != json.dumps(value):
                raise ValueError('public compile profile mismatch: ' + key)
    cache = (build / 'CMakeCache.txt').read_text()
    if 'DOODAD_MOQ_STREAM_SOAK:BOOL=OFF' not in cache:
        raise ValueError('synthetic diagnostics not explicitly disabled')
    image = (build / 'doodad_runtime.bin').read_bytes()
    if not 24 <= len(image) <= 0x400000 or image[0] != 0xe9:
        raise ValueError('invalid application image')
    if any(marker in image for marker in (b'VWMOQ1 SOAK', b'SOAK_FINAL', b'MOQ_STATUS uptime_ms=')):
        raise ValueError('synthetic firmware command linked')
    symbols = subprocess.check_output([nm, '-C', str(build / 'doodad_runtime.elf')], text=True)
    if ' esp_peer_' in symbols or 'voice_moq_diagnostic' in symbols:
        raise ValueError('WebRTC or diagnostic implementation linked')
    for symbol in ('esp_moq_endpoint_connect', 'wolfSSL_connect',
                   'doodad::moq_control::artifact_trust(',
                   'doodad::packages::package_service_offer('):
        if symbol not in symbols:
            raise ValueError('required implementation absent: ' + symbol)
    files = ('doodad_runtime.bin', 'doodad_runtime.elf', 'doodad_runtime.map')
    return dict(build_checks_pass=True, public_compile_profile=public_ci,
                hardware_acceptance=False, image_bytes=len(image),
                app_partition_bytes=0x400000, free_app_bytes=0x400000-len(image),
                hashes={name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in files})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--nm', default='xtensa-esp32s3-elf-nm')
    parser.add_argument('--public-ci', action='store_true')
    args = parser.parse_args()
    print(json.dumps(inspect(args.build, args.nm, args.public_ci), indent=2))
