#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=""
OUTPUT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="${2:?--port requires a serial device}"; shift 2 ;;
        --output) OUTPUT="${2:?--output requires a file}"; shift 2 ;;
        *) echo "Usage: $0 --port DEVICE [--output FILE]" >&2; exit 2 ;;
    esac
done
[[ -n "${PORT}" && -e "${PORT}" ]] || {
    echo "An existing --port is required." >&2; exit 1;
}

# This is deliberately explicit: entering the ROM loader resets the selected
# device. It does not write flash, eFuses, or security configuration.
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"
report="$({
    echo "port=${PORT}"
    python -m esptool --chip esp32s3 --port "${PORT}" read_mac
    python -m esptool --chip esp32s3 --port "${PORT}" flash_id
    python -m esptool --chip esp32s3 --port "${PORT}" get_security_info
} 2>&1)" || {
    printf '%s\n' "${report}" >&2
    echo "Probe failed. Put only the selected board in download mode and retry." >&2
    exit 1
}
printf '%s\n' "${report}"
if [[ -n "${OUTPUT}" ]]; then
    mkdir -p "$(dirname "${OUTPUT}")"
    [[ ! -e "${OUTPUT}" ]] || { echo "Refusing to overwrite ${OUTPUT}" >&2; exit 1; }
    printf '%s\n' "${report}" >"${OUTPUT}"
fi
