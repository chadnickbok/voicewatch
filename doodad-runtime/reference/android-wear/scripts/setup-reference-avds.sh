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
    "doodad_wear61_small_round" \
    "${WEAR_61_IMAGE}" \
    "wearos_small_round"

echo "Reference AVDs are ready:"
"${EMULATOR}" -list-avds | grep '^doodad_' || true
