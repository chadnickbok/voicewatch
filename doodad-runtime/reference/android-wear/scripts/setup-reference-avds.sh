#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

SDK_MANAGER="${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager"
AVD_MANAGER="${ANDROID_HOME}/cmdline-tools/latest/bin/avdmanager"
EMULATOR="${ANDROID_HOME}/emulator/emulator"

for tool in "${SDK_MANAGER}" "${AVD_MANAGER}" "${EMULATOR}"; do
    if [[ ! -x "${tool}" ]]; then
        echo "Missing Android SDK tool: ${tool}" >&2
        exit 1
    fi
done

case "$(uname -m)" in
    arm64|aarch64)
        IMAGE_ARCH="arm64-v8a"
        ;;
    x86_64)
        IMAGE_ARCH="x86_64"
        ;;
    *)
        echo "Unsupported host architecture: $(uname -m)" >&2
        exit 1
        ;;
esac

WEAR_7_IMAGE="system-images;android-37.0;android-wear-signed;${IMAGE_ARCH}"
WEAR_61_IMAGE="system-images;android-36.1;android-wear-signed;${IMAGE_ARCH}"

"${SDK_MANAGER}" --install \
    "platforms;android-37.0" \
    "build-tools;37.0.0" \
    "${WEAR_7_IMAGE}" \
    "${WEAR_61_IMAGE}"

create_avd() {
    local name="$1"
    local image="$2"
    local device="$3"

    if "${EMULATOR}" -list-avds | grep -Fxq "${name}"; then
        echo "AVD already exists: ${name}"
        return
    fi

    printf 'no\n' | "${AVD_MANAGER}" create avd \
        --name "${name}" \
        --package "${image}" \
        --device "${device}"
}

create_avd \
    "doodad_wear7_small_round" \
    "${WEAR_7_IMAGE}" \
    "wearos_small_round"
create_avd \
    "doodad_wear7_large_round" \
    "${WEAR_7_IMAGE}" \
    "wearos_large_round"
create_avd \
    "Wear_OS_Square" \
    "${WEAR_7_IMAGE}" \
    "wearos_square"
create_avd \
    "doodad_wear61_small_round" \
    "${WEAR_61_IMAGE}" \
    "wearos_small_round"

SQUARE_AVD_ROOT="${ANDROID_AVD_HOME:-${HOME}/.android/avd}"
SQUARE_CONFIG="${SQUARE_AVD_ROOT}/Wear_OS_Square.avd/config.ini"
python3 - "${SQUARE_CONFIG}" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(f"Square Wear AVD config not found: {path}")

updates = {
    "fastboot.forceColdBoot": "yes",
    "fastboot.forceFastBoot": "no",
    "hw.arc": "false",
    "hw.lcd.density": "200",
    "hw.lcd.height": "240",
    "hw.lcd.width": "240",
}
lines = path.read_text().splitlines()
seen = set()
output = []
for line in lines:
    key, separator, _ = line.partition("=")
    if separator and key in updates:
        output.append(f"{key}={updates[key]}")
        seen.add(key)
    else:
        output.append(line)
for key in sorted(set(updates) - seen):
    output.append(f"{key}={updates[key]}")
path.write_text("\n".join(output) + "\n")
PY

echo "Reference AVDs are ready:"
"${EMULATOR}" -list-avds | grep '^doodad_' || true
echo "Wear_OS_Square (API 37, 240x240, 200 dpi)"
