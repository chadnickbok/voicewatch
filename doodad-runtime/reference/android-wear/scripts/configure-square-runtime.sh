#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERIAL=""

usage() {
    echo "Usage: $0 [--serial SERIAL]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            SERIAL="${2:?--serial requires a value}"
            shift 2
            ;;
        -*)
            usage
            exit 2
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

# shellcheck source=android-env.sh
source "${SCRIPT_DIR}/android-env.sh"

if [[ -z "${SERIAL}" ]]; then
    online_devices="$(
        adb devices | awk 'NR > 1 && $2 == "device" {print $1}'
    )"
    online_count="$(
        printf '%s\n' "${online_devices}" |
            awk 'NF {count += 1} END {print count + 0}'
    )"
    if [[ "${online_count}" -ne 1 ]]; then
        echo "Exactly one online adb device is required, or pass --serial." >&2
        exit 1
    fi
    SERIAL="${online_devices}"
fi

adb_args=(-s "${SERIAL}")
sdk="$(adb "${adb_args[@]}" shell getprop ro.build.version.sdk | tr -d '\r')"
avd_name="$(
    adb "${adb_args[@]}" shell getprop ro.boot.qemu.avd_name | tr -d '\r'
)"

if [[ "${sdk}" != "37" ]]; then
    echo "Expected API 37, got API ${sdk} on ${SERIAL}." >&2
    exit 1
fi
if [[ "${avd_name}" != "Wear_OS_Square" ]]; then
    echo "Expected Wear_OS_Square, got ${avd_name:-an unnamed device}." >&2
    exit 1
fi

# The API 37 wearos_square skin currently boots with a 360x360 physical
# framebuffer even when config.ini requests 240x240. Android's supported
# display override gives the application and screencap lanes the exact product
# viewport without resampling.
adb "${adb_args[@]}" shell wm size 240x240
adb "${adb_args[@]}" shell wm density 200

size="$(adb "${adb_args[@]}" shell wm size | tr -d '\r')"
density="$(adb "${adb_args[@]}" shell wm density | tr -d '\r')"

if [[ "${size}" != *"Override size: 240x240"* ]]; then
    echo "Failed to establish the 240x240 runtime override: ${size}" >&2
    exit 1
fi
if [[ "${density}" != *"200"* ]]; then
    echo "Failed to establish Android density 200: ${density}" >&2
    exit 1
fi

echo "${SERIAL}: ${avd_name}, API ${sdk}"
echo "${size}"
echo "${density}"
