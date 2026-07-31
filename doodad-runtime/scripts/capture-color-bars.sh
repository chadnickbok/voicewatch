#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_DIR="$(cd "${PROJECT_DIR}/.." && pwd)"
PORT=""
# The calibration must use the exact acquisition settings used for shareable UI
# captures. Short exposure preserves text edges; fixed white balance makes the
# fitted correction transferable between the target and app screens.
EXPOSURE=16
GAIN=58
WHITE_BALANCE_TEMPERATURE=4000
MOIRE_SIGMA=0
OUTPUT_DIR="${PROJECT_DIR}/target/hardware-gallery/color-bars"
PROFILE_OUTPUT=""
CAPTURE_ONLY=0
CLEANCAM="${WORKSPACE_DIR}/.build/CleanCam.app/Contents/MacOS/CleanCam"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)
            PORT="${2:?--port requires a serial device}"
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
        --profile-output)
            PROFILE_OUTPUT="${2:?--profile-output requires a path}"
            shift 2
            ;;
        --capture-only)
            CAPTURE_ONLY=1
            shift
            ;;
        *)
            echo \
                "Usage: $0 [--port DEVICE] [--exposure N] [--gain N]" \
                "[--white-balance-temperature K] [--moire-sigma N]" \
                "[--output DIR] [--profile-output FILE] [--capture-only]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${PORT}" ]]; then
    PORT="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print 2>/dev/null | head -n 1)"
fi
if [[ "${CAPTURE_ONLY}" -eq 0 && (-z "${PORT}" || ! -e "${PORT}") ]]; then
    echo "No CoreS3 serial port found. Supply one with --port." >&2
    exit 1
fi
if [[ ! -x "${CLEANCAM}" ]]; then
    echo "CleanCam is not built: ${CLEANCAM}" >&2
    exit 1
fi
if ! command -v magick >/dev/null; then
    echo "ImageMagick is required for color-bar capture." >&2
    exit 1
fi
if [[ ! "${MOIRE_SIGMA}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "--moire-sigma must be a non-negative number" >&2
    exit 2
fi

mkdir -p "${OUTPUT_DIR}"

"${PROJECT_DIR}/doodad" \
    catalog \
    --story color-bars \
    --output "${OUTPUT_DIR}/reference.bmp"
magick \
    "${OUTPUT_DIR}/reference.bmp" \
    "${OUTPUT_DIR}/reference.png"

if [[ "${CAPTURE_ONLY}" -eq 0 ]]; then
    "${SCRIPT_DIR}/build-firmware.sh" --catalog-story color-bars
    "${SCRIPT_DIR}/flash.sh" --port "${PORT}" --no-monitor
fi

"${CLEANCAM}" \
    --capture "${OUTPUT_DIR}/camera-raw.png" \
    --exposure "${EXPOSURE}" \
    --gain "${GAIN}" \
    --white-balance-temperature "${WHITE_BALANCE_TEMPERATURE}" \
    --auto-focus

# Threshold the white registration frame to find the true watch viewport. This
# deliberately does not reuse the 20-app crop, which includes the physical
# CoreS3 gutters for presentation.
VIEWPORT_GEOMETRY="$(
    magick \
        "${OUTPUT_DIR}/camera-raw.png" \
        -colorspace Gray \
        -threshold 50% \
        -trim \
        -format '%wx%h%X%Y' \
        info:
)"
if [[ ! "${VIEWPORT_GEOMETRY}" =~ ^[0-9]+x[0-9]+\+[0-9]+\+[0-9]+$ ]]; then
    echo "Could not detect the color-bars registration frame." >&2
    exit 1
fi
magick \
    "${OUTPUT_DIR}/camera-raw.png" \
    -crop "${VIEWPORT_GEOMETRY}" \
    +repage \
    -filter Lanczos \
    -resize 240x240! \
    "${OUTPUT_DIR}/camera-crop-unfiltered.png"
if [[ "${MOIRE_SIGMA}" == "0" || "${MOIRE_SIGMA}" == "0.0" ]]; then
    magick \
        "${OUTPUT_DIR}/camera-crop-unfiltered.png" \
        "${OUTPUT_DIR}/camera-crop.png"
else
    magick \
        "${OUTPUT_DIR}/camera-raw.png" \
        -crop "${VIEWPORT_GEOMETRY}" \
        +repage \
        -blur "0x${MOIRE_SIGMA}" \
        -filter Lanczos \
        -resize 240x240! \
        "${OUTPUT_DIR}/camera-crop.png"
fi
python3 "${PROJECT_DIR}/tools/color_calibration/analyze.py" \
    "${OUTPUT_DIR}/camera-crop.png" \
    --csv "${OUTPUT_DIR}/measurements.csv" \
    --json "${OUTPUT_DIR}/calibration.json" \
    --corrected "${OUTPUT_DIR}/camera-corrected.png" \
    --exposure "${EXPOSURE}" \
    --gain "${GAIN}" \
    --white-balance-temperature "${WHITE_BALANCE_TEMPERATURE}" \
    --focus-mode auto \
    --viewport-geometry "${VIEWPORT_GEOMETRY}" \
    --camera "Logitech StreamCam" \
    --source-label "normalized color-bars capture"

if [[ -n "${PROFILE_OUTPUT}" ]]; then
    mkdir -p "$(dirname "${PROFILE_OUTPUT}")"
    cp "${OUTPUT_DIR}/calibration.json" "${PROFILE_OUTPUT}"
    echo "Installed capture profile: ${PROFILE_OUTPUT}"
fi

magick \
    "${OUTPUT_DIR}/reference.png" \
    "${OUTPUT_DIR}/camera-crop-unfiltered.png" \
    "${OUTPUT_DIR}/camera-crop.png" \
    "${OUTPUT_DIR}/camera-corrected.png" \
    +append \
    "${OUTPUT_DIR}/comparison.png"

echo "Color calibration capture complete: ${OUTPUT_DIR}"
