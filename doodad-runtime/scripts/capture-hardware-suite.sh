#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"
CORES3_PORT="/dev/cu.usbmodem21101"
WATCH_PORT="/dev/cu.usbmodem22301"
PROFILE="${PROJECT_DIR}/config/capture/streamcam-dual-v2.json"
OUTPUT="${PROJECT_DIR}/target/hardware-gallery/dual"
REFERENCE=""
SCENE=""
EXPECTED_SCREEN=""
EXPECTED_TEXT=()
CALIBRATE=0
FLASH=0
OFFLINE=0
CLEANCAM="${WORKSPACE_DIR}/.build/CleanCam.app/Contents/MacOS/CleanCam"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cores3-port) CORES3_PORT="${2:?--cores3-port requires a device}"; shift 2 ;;
        --watch-port) WATCH_PORT="${2:?--watch-port requires a device}"; shift 2 ;;
        --profile) PROFILE="${2:?--profile requires a file}"; shift 2 ;;
        --output) OUTPUT="${2:?--output requires a directory}"; shift 2 ;;
        --reference) REFERENCE="${2:?--reference requires a 240x240 image}"; shift 2 ;;
        --scene) SCENE="${2:?--scene requires the firmware scene name}"; shift 2 ;;
        --expected-screen) EXPECTED_SCREEN="${2:?--expected-screen requires a name}"; shift 2 ;;
        --expected-text) EXPECTED_TEXT+=("$2"); shift 2 ;;
        --calibrate) CALIBRATE=1; shift ;;
        --flash) FLASH=1; shift ;;
        --offline) OFFLINE=1; shift ;;
        *)
            echo "Usage: $0 [--calibrate] [--flash] [--profile FILE] [--output DIR]" >&2
            echo "  --scene NAME --reference PNG --expected-screen NAME [--expected-text TEXT]" >&2
            echo "  [--cores3-port DEVICE] [--watch-port DEVICE] [--offline]" >&2
            exit 2
            ;;
    esac
done

[[ -x "${CLEANCAM}" ]] || { echo "CleanCam is not built: ${CLEANCAM}" >&2; exit 1; }
if [[ "${OFFLINE}" -eq 0 ]]; then
    [[ -e "${CORES3_PORT}" && -e "${WATCH_PORT}" ]] || {
        echo "Both explicitly selected serial devices must be present." >&2; exit 1;
    }
fi

if [[ "${FLASH}" -eq 1 ]]; then
    if [[ "${CALIBRATE}" -eq 1 ]]; then
        "${SCRIPT_DIR}/build-firmware.sh" --board all --catalog-story color-bars
    else
        "${SCRIPT_DIR}/build-firmware.sh" --board all
    fi
    "${SCRIPT_DIR}/flash.sh" --board cores3 --port "${CORES3_PORT}" --no-monitor
    "${SCRIPT_DIR}/flash.sh" --board t-watch-s3 --port "${WATCH_PORT}" --no-monitor
fi

mkdir -p "${OUTPUT}"
offline_args=()
[[ "${OFFLINE}" -eq 1 ]] && offline_args+=(--offline)
if [[ "${CALIBRATE}" -eq 1 ]]; then
    registration="${OUTPUT}/registration-raw.png"
    rm -f "${registration}"
    python3 "${PROJECT_DIR}/tools/dual_capture.py" calibrate \
        --raw "${registration}" \
        --output "${PROFILE}" \
        --cleancam "${CLEANCAM}" \
        --cores3-port "${CORES3_PORT}" \
        --watch-port "${WATCH_PORT}" \
        "${offline_args[@]}"
    exit 0
fi

[[ -n "${SCENE}" && -n "${REFERENCE}" && -n "${EXPECTED_SCREEN}" ]] || {
    echo "--scene, --reference, and --expected-screen are required." >&2; exit 2;
}
[[ -f "${PROFILE}" ]] || {
    echo "Dual calibration is missing: ${PROFILE}; run with --calibrate first." >&2; exit 1;
}
[[ -f "${REFERENCE}" ]] || { echo "Reference does not exist: ${REFERENCE}" >&2; exit 1; }
raw="${OUTPUT}/${SCENE}-acquisition.png"
rm -f "${raw}"
text_args=()
for value in "${EXPECTED_TEXT[@]}"; do text_args+=(--expected-text "${value}"); done
python3 "${PROJECT_DIR}/tools/dual_capture.py" capture \
    --profile "${PROFILE}" \
    --raw "${raw}" \
    --cleancam "${CLEANCAM}" \
    --reference "${REFERENCE}" \
    --output "${OUTPUT}" \
    --scene "${SCENE}" \
    --expected-screen "${EXPECTED_SCREEN}" \
    --cores3-port "${CORES3_PORT}" \
    --watch-port "${WATCH_PORT}" \
    "${text_args[@]}" \
    "${offline_args[@]}"

echo "Open the raw frame, both corrected crops, and contact sheet before recording review:"
echo "  ${OUTPUT}/${SCENE}/raw/both-devices.png"
echo "  ${OUTPUT}/${SCENE}/corrected/cores3.png"
echo "  ${OUTPUT}/${SCENE}/corrected/t-watch-s3.png"
echo "  ${OUTPUT}/${SCENE}/contact-sheet.png"
