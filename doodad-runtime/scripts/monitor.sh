#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BOARD="cores3"
PORT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --board) BOARD="${2:?--board requires cores3 or t-watch-s3}"; shift 2 ;;
        --port) PORT="${2:?--port requires a serial device}"; shift 2 ;;
        *) echo "Usage: $0 --board cores3|t-watch-s3 --port DEVICE" >&2; exit 2 ;;
    esac
done
[[ "${BOARD}" == "cores3" || "${BOARD}" == "t-watch-s3" ]] || {
    echo "Unknown board: ${BOARD}" >&2; exit 2;
}
if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
    echo "An unambiguous existing --port is required when monitoring." >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"
idf.py -C "${PROJECT_DIR}/firmware" \
    -B "${PROJECT_DIR}/firmware/build/${BOARD}" -p "${PORT}" monitor
