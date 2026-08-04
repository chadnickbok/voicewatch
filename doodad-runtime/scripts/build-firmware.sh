#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_SLUG="hello"
SHOW_APP=0
CATALOG_STORY=-1
BOARD="cores3"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            APP_SLUG="${2:?--app requires an app slug}"
            shift 2
            ;;
        --show-app)
            SHOW_APP=1
            shift
            ;;
        --catalog-story)
            case "${2:?--catalog-story requires a story name}" in
                color-bars)
                    CATALOG_STORY=33
                    ;;
                *)
                    echo "Unknown boot catalog story: $2" >&2
                    exit 2
                    ;;
            esac
            shift 2
            ;;
        --board)
            BOARD="${2:?--board requires cores3, t-watch-s3, or all}"
            case "${BOARD}" in
                cores3|t-watch-s3|all) ;;
                *)
                    echo "Unknown board: ${BOARD}" >&2
                    exit 2
                    ;;
            esac
            shift 2
            ;;
        *)
            echo \
                "Usage: $0 [--board cores3|t-watch-s3|all]" \
                "[--app APP_SLUG] [--show-app]" \
                "[--catalog-story color-bars]" >&2
            exit 2
            ;;
    esac
done

if [[ "${BOARD}" == "all" ]]; then
    forwarded=(--app "${APP_SLUG}")
    if [[ "${SHOW_APP}" -eq 1 ]]; then
        forwarded+=(--show-app)
    fi
    if [[ "${CATALOG_STORY}" -eq 33 ]]; then
        forwarded+=(--catalog-story color-bars)
    fi
    "$0" --board cores3 "${forwarded[@]}"
    "$0" --board t-watch-s3 "${forwarded[@]}"
    exit 0
fi

if [[ "${SHOW_APP}" -eq 1 && "${CATALOG_STORY}" -ge 0 ]]; then
    echo "--show-app and --catalog-story are mutually exclusive" >&2
    exit 2
fi

"${SCRIPT_DIR}/build-guest.sh" "${APP_SLUG}"

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"

BUILD_DIR="${PROJECT_DIR}/firmware/build/${BOARD}"
SDKCONFIG="${PROJECT_DIR}/firmware/sdkconfig.${BOARD}"
SDKCONFIG_DEFAULTS="${PROJECT_DIR}/firmware/sdkconfig.defaults;${PROJECT_DIR}/firmware/boards/${BOARD}/sdkconfig.defaults"
SDKCONFIG_READY=true
if [[ "${BOARD}" == "cores3" ]]; then
    BOARD_SETTING='CONFIG_DOODAD_BOARD_CORES3=y'
    PSRAM_SETTING='CONFIG_SPIRAM_MODE_QUAD=y'
else
    BOARD_SETTING='CONFIG_DOODAD_BOARD_TWATCH_S3=y'
    PSRAM_SETTING='CONFIG_SPIRAM_MODE_OCT=y'
fi
for setting in \
    'CONFIG_IDF_TARGET="esp32s3"' \
    'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y' \
    'CONFIG_FREERTOS_HZ=1000' \
    'CONFIG_LV_COLOR_DEPTH_16=y' \
    'CONFIG_LV_DEF_REFR_PERIOD=8' \
    'CONFIG_LV_FONT_MONTSERRAT_10=y' \
    'CONFIG_LV_FONT_MONTSERRAT_12=y' \
    'CONFIG_LV_FONT_MONTSERRAT_16=y' \
    'CONFIG_LV_FONT_MONTSERRAT_18=y' \
    'CONFIG_DOODAD_VOICE_UPLINK=y' \
    'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' \
    "${BOARD_SETTING}" \
    "${PSRAM_SETTING}"; do
    if ! grep -q "^${setting}$" "${SDKCONFIG}" 2>/dev/null; then
        SDKCONFIG_READY=false
        break
    fi
done

if [[ "${SDKCONFIG_READY}" != true ]]; then
    if [[ -f "${SDKCONFIG}" ]]; then
        mv -f "${SDKCONFIG}" "${SDKCONFIG}.old"
    fi
    echo "Refreshing ${BOARD} sdkconfig from board defaults"
    idf.py -C "${PROJECT_DIR}/firmware" -B "${BUILD_DIR}" \
        -D SDKCONFIG="${SDKCONFIG}" \
        -D SDKCONFIG_DEFAULTS="${SDKCONFIG_DEFAULTS}" \
        set-target esp32s3
fi
if [[ "${SHOW_APP}" -eq 1 ]]; then
    idf.py \
        -C "${PROJECT_DIR}/firmware" \
        -B "${BUILD_DIR}" \
        -D SDKCONFIG="${SDKCONFIG}" \
        -D SDKCONFIG_DEFAULTS="${SDKCONFIG_DEFAULTS}" \
        -D DOODAD_SHOW_APP_AT_BOOT=ON \
        -D DOODAD_BOOT_CATALOG_STORY=-1 \
        build
else
    idf.py \
        -C "${PROJECT_DIR}/firmware" \
        -B "${BUILD_DIR}" \
        -D SDKCONFIG="${SDKCONFIG}" \
        -D SDKCONFIG_DEFAULTS="${SDKCONFIG_DEFAULTS}" \
        -D DOODAD_SHOW_APP_AT_BOOT=OFF \
        -D DOODAD_BOOT_CATALOG_STORY="${CATALOG_STORY}" \
        build
fi

ARTIFACT="${BUILD_DIR}/doodad_runtime.bin"
[[ -s "${ARTIFACT}" ]] || { echo "Missing firmware artifact: ${ARTIFACT}" >&2; exit 1; }
ARTIFACT_SIZE="$(stat -f '%z' "${ARTIFACT}")"
if [[ "${ARTIFACT_SIZE}" -ge $((3 * 1024 * 1024)) ]]; then
    echo "${BOARD} artifact exceeds the 3 MiB OTA partition: ${ARTIFACT_SIZE}" >&2
    exit 1
fi
echo "${BOARD} artifact: ${ARTIFACT} (${ARTIFACT_SIZE} bytes)"
