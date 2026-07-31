#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SCENARIO_DIR="$(cd "${PROJECT_DIR}/.." && pwd)/scenarios"
PACKAGE="dev.doodad.reference"
SERIAL=""
PHASE="resting"
OUTPUT_DIR="${PROJECT_DIR}/captures"

usage() {
    echo "Usage: $0 [--serial SERIAL] [--phase NAME] [--output DIR] SCENE" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            SERIAL="${2:?--serial requires a value}"
            shift 2
            ;;
        --phase)
            PHASE="${2:?--phase requires a value}"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="${2:?--output requires a value}"
            shift 2
            ;;
        -*)
            usage
            exit 2
            ;;
        *)
            SCENE="$1"
            shift
            ;;
    esac
done

if [[ -z "${SCENE:-}" ]]; then
    usage
    exit 2
fi

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

python3 - "${SCENARIO_DIR}" "${SCENE}" <<'PY'
import json
import pathlib
import sys

scenario_dir = pathlib.Path(sys.argv[1])
requested = sys.argv[2]
index = json.loads((scenario_dir / "index.json").read_text())
scenarios = [
    json.loads((scenario_dir / name).read_text())
    for name in index["scenarios"]
]
if not any(item["scene"] == requested or item["id"] == requested for item in scenarios):
    raise SystemExit(f"unknown reference scene: {requested}")
PY

adb_args=()
if [[ -n "${SERIAL}" ]]; then
    adb_args=(-s "${SERIAL}")
fi

mkdir -p "${OUTPUT_DIR}"
base="${OUTPUT_DIR}/${SCENE}.${PHASE}"
remote_semantics="/data/local/tmp/doodad-reference-semantics.xml"

adb "${adb_args[@]}" shell am start -S -W \
    -n "${PACKAGE}/.MainActivity" \
    --es oracle_scene "${SCENE}" >/dev/null
sleep 1.2

adb "${adb_args[@]}" exec-out screencap -p >"${base}.png"
adb "${adb_args[@]}" shell uiautomator dump "${remote_semantics}" >/dev/null
adb "${adb_args[@]}" exec-out cat "${remote_semantics}" >"${base}.semantics.xml"
adb "${adb_args[@]}" shell rm "${remote_semantics}"
{
    adb "${adb_args[@]}" shell wm size
    adb "${adb_args[@]}" shell wm density
    adb "${adb_args[@]}" shell dumpsys activity activities |
        grep -m1 "mResumedActivity" || true
} >"${base}.device.txt"

echo "Captured ${base}.png"
echo "Captured ${base}.semantics.xml"
