#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
HOST_SOURCE="${PROJECT_DIR}/tools/native-host"
HOST_BUILD="${HOST_SOURCE}/build"

if [[ ! -f \
    "${PROJECT_DIR}/firmware/managed_components/lvgl__lvgl/CMakeLists.txt" \
    || ! -f \
    "${PROJECT_DIR}/firmware/managed_components/espressif__wasm-micro-runtime/build-scripts/runtime_lib.cmake" ]]; then
    # shellcheck disable=SC1091
    source "${SCRIPT_DIR}/env.sh"
    idf.py -C "${PROJECT_DIR}/firmware" reconfigure
fi

cmake \
    -S "${HOST_SOURCE}" \
    -B "${HOST_BUILD}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${HOST_BUILD}" --target doodad_native_host
if [[ "${DOODAD_BUILD_TESTS:-1}" == "1" ]]; then
    cmake --build "${HOST_BUILD}" --target \
        m3e_display_profile_test \
        m3e_core_tokens_test \
        m3e_framework_test
fi

echo "Native simulator host built: ${HOST_BUILD}/libdoodad_native_host.dylib"
