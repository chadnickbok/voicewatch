#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SERIAL=""

if [[ "${1:-}" == "--serial" ]]; then
    SERIAL="${2:?--serial requires an emulator or device serial}"
fi

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

cd "${PROJECT_DIR}"
./gradlew :app:assembleDebug

adb_args=()
if [[ -n "${SERIAL}" ]]; then
    adb_args=(-s "${SERIAL}")
fi

adb "${adb_args[@]}" install -r \
    "${PROJECT_DIR}/app/build/outputs/apk/debug/app-debug.apk"
