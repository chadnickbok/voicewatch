#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_DIR}"

python3 -m unittest tests.test_conformance tests.test_conformance_suite
cargo build --locked --release --target wasm32-unknown-unknown --workspace

while IFS= read -r app; do
    ./doodad test "${app}"
done < <(
    python3 -c '
import json
from pathlib import Path
catalog = json.loads(Path("apps/conformance-suite.json").read_text())
for app in catalog["apps"]:
    print(app["slug"])
'
)

echo "All 20 conformance packages built, validated, and executed in WAMR."
