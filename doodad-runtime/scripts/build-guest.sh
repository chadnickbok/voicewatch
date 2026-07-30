#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GUEST_WASM="${PROJECT_DIR}/target/wasm32-unknown-unknown/release/doodad_hello.wasm"
EMBEDDED_WASM="${PROJECT_DIR}/firmware/main/embedded/hello.wasm"

cd "${PROJECT_DIR}"
rustup target add wasm32-unknown-unknown --toolchain 1.95.0 >/dev/null
cargo build \
    --locked \
    --release \
    --target wasm32-unknown-unknown \
    --package doodad-hello

mkdir -p "$(dirname "${EMBEDDED_WASM}")"
cp "${GUEST_WASM}" "${EMBEDDED_WASM}"
python3 "${SCRIPT_DIR}/inspect-wasm.py" "${EMBEDDED_WASM}" --verify-hello

echo "Embedded guest updated: ${EMBEDDED_WASM}"
