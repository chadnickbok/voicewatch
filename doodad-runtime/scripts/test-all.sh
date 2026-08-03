#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_DIR}"

python3 tools/token_sync/sync.py --check
python3 tools/weather_foundations/generate.py --check
python3 tools/weather_foundations/generate_fonts.py --check
PYTHONPATH=tools python3 tools/generate_media_asset.py --check
PYTHONPATH=tools python3 tools/generate_wallet_asset.py --check
PYTHONPATH=tools python3 tools/generate_remote_asset.py --check
./scripts/build-native-host.sh
ctest --test-dir tools/native-host/build --output-on-failure
python3 -m unittest discover -s tests
cargo test --locked --package doodad-sdk

./scripts/test-conformance-suite.sh
python3 tools/generate_conformance_evidence.py --check
PYTHONPATH=tools python3 tools/generate_parallax_traces.py --check
PYTHONPATH=tools python3 tools/generate_parallax_inventory.py --check
./doodad appspec "apps/voice/appspec.json" --validate-only

./doodad test hello
./scripts/build-firmware.sh

echo "All desktop, contract, golden, WAMR, and firmware checks passed."
