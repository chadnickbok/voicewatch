#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ORACLE_DIR="${PROJECT_DIR}/reference/android-wear"

cd "${PROJECT_DIR}"
python3 -m unittest tests.test_reference_scenarios

# shellcheck source=../reference/android-wear/scripts/android-env.sh
source "${ORACLE_DIR}/scripts/android-env.sh"
cd "${ORACLE_DIR}"
./gradlew :app:testDebugUnitTest :app:verifyRoborazziDebug
