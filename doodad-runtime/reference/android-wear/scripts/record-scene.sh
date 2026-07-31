#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SERIAL=""
OUTPUT_DIR="${PROJECT_DIR}/captures"
DURATION=15

if [[ "${1:-}" == "--serial" ]]; then
    SERIAL="${2:?--serial requires a value}"
    shift 2
fi
SCENE="${1:?Usage: $0 [--serial SERIAL] SCENE [DURATION_SECONDS]}"
DURATION="${2:-${DURATION}}"

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

adb_args=()
if [[ -n "${SERIAL}" ]]; then
    adb_args=(-s "${SERIAL}")
fi

mkdir -p "${OUTPUT_DIR}"
remote="/data/local/tmp/${SCENE}.mp4"
local_file="${OUTPUT_DIR}/${SCENE}.normal-speed.mp4"

adb "${adb_args[@]}" shell am start -S -W \
    -n "dev.doodad.reference/.MainActivity" \
    --es oracle_scene "${SCENE}" >/dev/null
adb "${adb_args[@]}" shell screenrecord \
    --time-limit "${DURATION}" \
    "${remote}"
adb "${adb_args[@]}" pull "${remote}" "${local_file}" >/dev/null
adb "${adb_args[@]}" shell rm "${remote}"

echo "Recorded ${local_file}"
