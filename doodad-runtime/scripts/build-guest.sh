#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_SLUG="${1:-hello}"
APP_DIR="${PROJECT_DIR}/apps/${APP_SLUG}"
APP_MANIFEST="${APP_DIR}/Cargo.toml"
if [[ ! "${APP_SLUG}" =~ ^[a-z0-9-]+$ || ! -f "${APP_MANIFEST}" ]]; then
    echo "Unknown app: ${APP_SLUG}" >&2
    exit 2
fi
PACKAGE_NAME="$(
    sed -n 's/^name = "\([^"]*\)"/\1/p' "${APP_MANIFEST}" | head -n 1
)"
if [[ -z "${PACKAGE_NAME}" ]]; then
    echo "No package name found in ${APP_MANIFEST}" >&2
    exit 2
fi
WASM_STEM="${PACKAGE_NAME//-/_}"
GUEST_WASM="${PROJECT_DIR}/target/wasm32-unknown-unknown/release/${WASM_STEM}.wasm"
EMBEDDED_WASM="${PROJECT_DIR}/firmware/main/embedded/hello.wasm"

cd "${PROJECT_DIR}"
rustup target add wasm32-unknown-unknown --toolchain 1.95.0 >/dev/null
cargo build \
    --locked \
    --release \
    --target wasm32-unknown-unknown \
    --package "${PACKAGE_NAME}"

mkdir -p "$(dirname "${EMBEDDED_WASM}")"
cp "${GUEST_WASM}" "${EMBEDDED_WASM}"
python3 "${SCRIPT_DIR}/inspect-wasm.py" "${EMBEDDED_WASM}" --verify-guest

echo "Embedded guest updated (${APP_SLUG}): ${EMBEDDED_WASM}"
