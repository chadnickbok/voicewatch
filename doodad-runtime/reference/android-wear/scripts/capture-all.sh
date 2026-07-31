#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SCENARIO_DIR="$(cd "${PROJECT_DIR}/.." && pwd)/scenarios"
SERIAL_ARGS=()
OUTPUT_ARGS=()

if [[ "${1:-}" == "--serial" ]]; then
    SERIAL_ARGS=(--serial "${2:?--serial requires a value}")
    shift 2
fi
if [[ "${1:-}" == "--output" ]]; then
    OUTPUT_ARGS=(--output "${2:?--output requires a value}")
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo "Usage: $0 [--serial SERIAL] [--output DIR]" >&2
    exit 2
fi

while IFS= read -r scene; do
    "${SCRIPT_DIR}/capture-scene.sh" \
        "${SERIAL_ARGS[@]}" \
        "${OUTPUT_ARGS[@]}" \
        "${scene}"
done < <(
    python3 - "${SCENARIO_DIR}" <<'PY'
import json
import pathlib
import sys

scenario_dir = pathlib.Path(sys.argv[1])
index = json.loads((scenario_dir / "index.json").read_text())
for name in index["scenarios"]:
    print(json.loads((scenario_dir / name).read_text())["scene"])
PY
)
