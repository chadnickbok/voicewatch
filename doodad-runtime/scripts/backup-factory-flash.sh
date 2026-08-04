#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT=""
OUTPUT=""
FLASH_SIZE="0x1000000"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="${2:?--port requires a serial device}"; shift 2 ;;
        --output) OUTPUT="${2:?--output requires a private backup path}"; shift 2 ;;
        --flash-size) FLASH_SIZE="${2:?--flash-size requires bytes}"; shift 2 ;;
        *) echo "Usage: $0 --port DEVICE --output FILE [--flash-size 0x1000000]" >&2; exit 2 ;;
    esac
done
[[ -n "${PORT}" && -e "${PORT}" ]] || { echo "An existing --port is required." >&2; exit 1; }
[[ -n "${OUTPUT}" ]] || { echo "A private --output path is required." >&2; exit 1; }
[[ ! -e "${OUTPUT}" ]] || { echo "Refusing to overwrite ${OUTPUT}" >&2; exit 1; }
mkdir -p "$(dirname "${OUTPUT}")"

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"
python -m esptool --chip esp32s3 --port "${PORT}" \
    --before default_reset --after no_reset read_flash 0 "${FLASH_SIZE}" "${OUTPUT}"
actual_size="$(stat -f '%z' "${OUTPUT}")"
expected_size="$((FLASH_SIZE))"
if [[ "${actual_size}" -ne "${expected_size}" ]]; then
    echo "Backup size mismatch: expected ${expected_size}, got ${actual_size}" >&2
    exit 1
fi
shasum -a 256 "${OUTPUT}" | tee "${OUTPUT}.sha256"
chmod 600 "${OUTPUT}" "${OUTPUT}.sha256"
echo "Factory flash backup complete; keep this file private: ${OUTPUT}"
