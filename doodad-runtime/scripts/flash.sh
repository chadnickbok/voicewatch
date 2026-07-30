#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORT=""
OPEN_MONITOR=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            PORT="${2:?--port requires a serial device}"
            shift 2
            ;;
        --no-monitor)
            OPEN_MONITOR=0
            shift
            ;;
        *)
            echo "Usage: $0 [--port /dev/cu.usbmodem...] [--no-monitor]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${PORT}" ]]; then
    PORT="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | head -n 1)"
fi
if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
    echo "No CoreS3 serial port found. Supply one with --port." >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"

idf.py -C "${PROJECT_DIR}/firmware" -p "${PORT}" flash
if [[ "${OPEN_MONITOR}" -eq 1 ]]; then
    idf.py -C "${PROJECT_DIR}/firmware" -p "${PORT}" monitor
fi
