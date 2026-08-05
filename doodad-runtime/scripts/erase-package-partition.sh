#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORT="${1:-}"

if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
    echo "Usage: $0 /dev/cu.usbmodem..." >&2
    exit 2
fi

# Clean-break transition from unsigned visual identity/DDR2 to signed
# identity/DDR3. This erases only the package FAT partition, preserving NVS,
# Wi-Fi provisioning, factory data, and both firmware slots.
source "${SCRIPT_DIR}/env.sh"
python -m esptool --chip esp32s3 --port "${PORT}" \
    erase-region 0x610000 0x9f0000
echo "Erased only the personal package partition"
