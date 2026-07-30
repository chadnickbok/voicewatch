#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_WASM="${PROJECT_DIR}/firmware/main/embedded/hello.wasm"
VOLUME_PATH="${1:-}"

if [[ -z "${VOLUME_PATH}" || ! -d "${VOLUME_PATH}" ]]; then
    echo "Usage: $0 /Volumes/YOUR_SD_CARD" >&2
    exit 2
fi
if [[ "${VOLUME_PATH}" != /Volumes/* ]]; then
    echo "Refusing to write outside a mounted macOS volume: ${VOLUME_PATH}" >&2
    exit 1
fi
if [[ ! -f "${SOURCE_WASM}" ]]; then
    echo "Guest is not built. Run ./scripts/build-guest.sh first." >&2
    exit 1
fi

DESTINATION_DIR="${VOLUME_PATH}/doodad"
DESTINATION_WASM="${DESTINATION_DIR}/hello.wasm"
mkdir -p "${DESTINATION_DIR}"
cp "${SOURCE_WASM}" "${DESTINATION_WASM}"

if ! cmp -s "${SOURCE_WASM}" "${DESTINATION_WASM}"; then
    echo "Verification failed after copying ${DESTINATION_WASM}." >&2
    exit 1
fi

sync
echo "Installed exact embedded guest bytes to ${DESTINATION_WASM}"
shasum -a 256 "${SOURCE_WASM}" "${DESTINATION_WASM}"
