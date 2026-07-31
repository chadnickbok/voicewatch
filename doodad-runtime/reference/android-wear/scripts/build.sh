#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

cd "${PROJECT_DIR}"
./gradlew :app:assembleDebug "$@"
