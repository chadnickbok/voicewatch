#!/usr/bin/env bash

set -euo pipefail

DOODAD_IDF_VERSION="v5.5.5"
DOODAD_IDF_PATH="${IDF_PATH:-${HOME}/.espressif/frameworks/esp-idf-${DOODAD_IDF_VERSION}}"

if command -v asdf >/dev/null 2>&1; then
    DOODAD_PYTHON_BIN="$(asdf which python3 2>/dev/null || true)"
    if [[ -n "${DOODAD_PYTHON_BIN}" ]]; then
        PATH="$(dirname "${DOODAD_PYTHON_BIN}"):${PATH}"
        export PATH
    fi
fi

if [[ ! -f "${DOODAD_IDF_PATH}/export.sh" ]]; then
    echo "ESP-IDF ${DOODAD_IDF_VERSION} is not installed at ${DOODAD_IDF_PATH}." >&2
    echo "Run ./scripts/bootstrap-macos.sh first, or set IDF_PATH." >&2
    return 1 2>/dev/null || exit 1
fi

# ESP-IDF's export script intentionally sets the active toolchain environment.
# shellcheck disable=SC1090
source "${DOODAD_IDF_PATH}/export.sh" >/dev/null
