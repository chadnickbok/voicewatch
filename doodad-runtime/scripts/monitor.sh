#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORT="${1:-}"

if [[ -z "${PORT}" ]]; then
    PORT="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | head -n 1)"
fi
if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
    echo "No CoreS3 serial port found. Pass the port as the first argument." >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"
idf.py -C "${PROJECT_DIR}/firmware" -p "${PORT}" monitor
