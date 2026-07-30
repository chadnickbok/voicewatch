#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
IDF_VERSION="v5.5.5"
IDF_INSTALL_DIR="${HOME}/.espressif/frameworks/esp-idf-${IDF_VERSION}"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "This bootstrap script supports Apple Silicon macOS." >&2
    exit 1
fi

for command_name in git cmake ninja python3 cargo rustc rustup; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Missing required command: ${command_name}" >&2
        exit 1
    fi
done

if command -v asdf >/dev/null 2>&1; then
    BOOTSTRAP_PYTHON_BIN="$(asdf which python3 2>/dev/null || true)"
    if [[ -n "${BOOTSTRAP_PYTHON_BIN}" ]]; then
        PATH="$(dirname "${BOOTSTRAP_PYTHON_BIN}"):${PATH}"
        export PATH
    fi
fi

if [[ ! -d "${IDF_INSTALL_DIR}/.git" ]]; then
    mkdir -p "$(dirname "${IDF_INSTALL_DIR}")"
    git clone \
        --branch "${IDF_VERSION}" \
        --depth 1 \
        --recursive \
        https://github.com/espressif/esp-idf.git \
        "${IDF_INSTALL_DIR}"
fi

if [[ ! -x "${HOME}/.espressif/python_env/idf5.5_py3.12_env/bin/python" ]]; then
    "${IDF_INSTALL_DIR}/install.sh" esp32s3
fi

rustup target add wasm32-unknown-unknown --toolchain 1.95.0

echo "Bootstrap complete."
echo "ESP-IDF: ${IDF_INSTALL_DIR}"
echo "Rust: $(cd "${PROJECT_DIR}" && rustc --version)"
