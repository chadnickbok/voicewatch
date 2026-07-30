#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_DIR}"

python3 tools/token_sync/sync.py --check
./scripts/build-native-host.sh
ctest --test-dir tools/native-host/build --output-on-failure
python3 -m unittest discover -s tests
cargo test --locked --package doodad-sdk

for app in calories calculator workout voice; do
    ./doodad appspec "apps/${app}/appspec.json" --validate-only
done

./doodad test hello
./scripts/build-firmware.sh

echo "All desktop, contract, golden, WAMR, and firmware checks passed."
