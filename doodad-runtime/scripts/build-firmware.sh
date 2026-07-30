#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

"${SCRIPT_DIR}/build-guest.sh"

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"

if ! grep -q '^CONFIG_IDF_TARGET="esp32s3"$' "${PROJECT_DIR}/firmware/sdkconfig" 2>/dev/null; then
    idf.py -C "${PROJECT_DIR}/firmware" set-target esp32s3
fi
idf.py -C "${PROJECT_DIR}/firmware" build
