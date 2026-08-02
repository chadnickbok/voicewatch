#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"
PORT=""
ONLY_APP=""
START_AT=""
EXPOSURE=16
GAIN=58
WHITE_BALANCE_TEMPERATURE=4000
MOIRE_SIGMA=0
OUTPUT_DIR="${PROJECT_DIR}/target/hardware-gallery/apps"
CALIBRATION_PROFILE="${PROJECT_DIR}/config/capture/streamcam-cores3-sharp.json"
CLEANCAM="${WORKSPACE_DIR}/.build/CleanCam.app/Contents/MacOS/CleanCam"
CAPTURE_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            PORT="${2:?--port requires a serial device}"
            shift 2
            ;;
        --app)
            ONLY_APP="${2:?--app requires an app slug}"
            shift 2
            ;;
        --start-at)
            START_AT="${2:?--start-at requires an app slug}"
            shift 2
            ;;
        --exposure)
            EXPOSURE="${2:?--exposure requires a value}"
            shift 2
            ;;
        --gain)
            GAIN="${2:?--gain requires a value}"
            shift 2
            ;;
        --white-balance-temperature)
            WHITE_BALANCE_TEMPERATURE="${2:?--white-balance-temperature requires a value}"
            shift 2
            ;;
        --moire-sigma)
            MOIRE_SIGMA="${2:?--moire-sigma requires a value}"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="${2:?--output requires a directory}"
            shift 2
            ;;
        --calibration-profile)
            CALIBRATION_PROFILE="${2:?--calibration-profile requires a file}"
            shift 2
            ;;
        --capture-only)
            CAPTURE_ONLY=1
            shift
            ;;
        *)
            echo \
                "Usage: $0 [--port DEVICE] [--app SLUG] [--start-at SLUG]" \
                "[--exposure N] [--gain N] [--white-balance-temperature K]" \
                "[--moire-sigma N] [--calibration-profile FILE]" \
                "[--output DIR] [--capture-only]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${PORT}" ]]; then
    PORT="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | head -n 1)"
fi
if [[ -z "${PORT}" || ! -e "${PORT}" ]]; then
    echo "No CoreS3 serial port found. Supply one with --port." >&2
    exit 1
fi
if [[ ! -x "${CLEANCAM}" ]]; then
    echo "CleanCam is not built: ${CLEANCAM}" >&2
    exit 1
fi
if ! command -v magick >/dev/null; then
    echo "ImageMagick is required for the comparison sheets." >&2
    exit 1
fi
if [[ ! -f "${CALIBRATION_PROFILE}" ]]; then
    echo "Capture calibration is missing: ${CALIBRATION_PROFILE}" >&2
    echo "Run scripts/capture-color-bars.sh --profile-output ${CALIBRATION_PROFILE}" >&2
    exit 1
fi
if [[ ! "${MOIRE_SIGMA}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "--moire-sigma must be a non-negative number" >&2
    exit 2
fi
VIEWPORT_GEOMETRY="$(
    python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["capture"]["viewport_geometry"])' \
        "${CALIBRATION_PROFILE}"
)"
if [[ ! "${VIEWPORT_GEOMETRY}" =~ ^[0-9]+x[0-9]+\+[0-9]+\+[0-9]+$ ]]; then
    echo "Calibration has invalid viewport geometry: ${VIEWPORT_GEOMETRY}" >&2
    exit 1
fi

mkdir -p \
    "${OUTPUT_DIR}/desktop" \
    "${OUTPUT_DIR}/hardware-raw" \
    "${OUTPUT_DIR}/hardware-crop-unfiltered" \
    "${OUTPUT_DIR}/hardware-crop" \
    "${OUTPUT_DIR}/hardware-corrected" \
    "${OUTPUT_DIR}/comparison"

ALL_APPS=()
while IFS= read -r app; do
    ALL_APPS+=("${app}")
done < <(
    sed -n \
        's/.*"slug": "\([^"]*\)".*/\1/p' \
        "${PROJECT_DIR}/apps/conformance-suite.json"
)
if [[ -n "${ONLY_APP}" ]]; then
    APPS=("${ONLY_APP}")
else
    APPS=("${ALL_APPS[@]}")
fi

started=false
if [[ -z "${START_AT}" ]]; then
    started=true
fi

for app in "${APPS[@]}"; do
    if [[ "${started}" != true ]]; then
        if [[ "${app}" == "${START_AT}" ]]; then
            started=true
        else
            continue
        fi
    fi
    if [[ ! -f "${PROJECT_DIR}/apps/${app}/appspec.json" ]]; then
        echo "Unknown conformance app: ${app}" >&2
        exit 2
    fi

    echo "=== Hardware conformance: ${app} ==="
    "${PROJECT_DIR}/doodad" \
        appspec \
        "${PROJECT_DIR}/apps/${app}/appspec.json" \
        --output "${OUTPUT_DIR}/desktop/${app}.bmp"
    magick \
        "${OUTPUT_DIR}/desktop/${app}.bmp" \
        "${OUTPUT_DIR}/desktop/${app}.png"

    if [[ "${CAPTURE_ONLY}" -eq 0 ]]; then
        "${SCRIPT_DIR}/build-firmware.sh" --app "${app}" --show-app
        "${SCRIPT_DIR}/flash.sh" --port "${PORT}" --no-monitor
    fi
    "${CLEANCAM}" \
        --capture "${OUTPUT_DIR}/hardware-raw/${app}.png" \
        --exposure "${EXPOSURE}" \
        --gain "${GAIN}" \
        --white-balance-temperature "${WHITE_BALANCE_TEMPERATURE}" \
        --auto-focus

    # The geometry comes from the same registered color-bars frame that fitted
    # the correction matrix. Moving the fixture requires a profile recapture.
    magick \
        "${OUTPUT_DIR}/hardware-raw/${app}.png" \
        -crop "${VIEWPORT_GEOMETRY}" \
        +repage \
        -filter Lanczos \
        -resize 240x240! \
        "${OUTPUT_DIR}/hardware-crop-unfiltered/${app}.png"
    if [[ "${MOIRE_SIGMA}" == "0" || "${MOIRE_SIGMA}" == "0.0" ]]; then
        magick \
            "${OUTPUT_DIR}/hardware-crop-unfiltered/${app}.png" \
            "${OUTPUT_DIR}/hardware-crop/${app}.png"
    else
        magick \
            "${OUTPUT_DIR}/hardware-raw/${app}.png" \
            -crop "${VIEWPORT_GEOMETRY}" \
            +repage \
            -blur "0x${MOIRE_SIGMA}" \
            -filter Lanczos \
            -resize 240x240! \
            "${OUTPUT_DIR}/hardware-crop/${app}.png"
    fi
    python3 "${PROJECT_DIR}/tools/color_calibration/apply.py" \
        "${OUTPUT_DIR}/hardware-crop/${app}.png" \
        "${OUTPUT_DIR}/hardware-corrected/${app}.png" \
        --profile "${CALIBRATION_PROFILE}" \
        --exposure "${EXPOSURE}" \
        --gain "${GAIN}" \
        --white-balance-temperature "${WHITE_BALANCE_TEMPERATURE}" \
        --focus-mode auto
    magick \
        "${OUTPUT_DIR}/desktop/${app}.png" \
        "${OUTPUT_DIR}/hardware-corrected/${app}.png" \
        +append \
        "${OUTPUT_DIR}/comparison/${app}.png"
done

echo "Hardware gallery complete: ${OUTPUT_DIR}"
