#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_SLUG="hello"
SHOW_APP=0
CATALOG_STORY=-1

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
        *)
            echo \
                "Usage: $0 [--app APP_SLUG] [--show-app]" \
                "[--catalog-story color-bars]" >&2
            exit 2
            ;;
    esac
done

if [[ "${SHOW_APP}" -eq 1 && "${CATALOG_STORY}" -ge 0 ]]; then
    echo "--show-app and --catalog-story are mutually exclusive" >&2
    exit 2
fi

"${SCRIPT_DIR}/build-guest.sh" "${APP_SLUG}"

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"

SDKCONFIG="${PROJECT_DIR}/firmware/sdkconfig"
SDKCONFIG_READY=true
for setting in \
    'CONFIG_IDF_TARGET="esp32s3"' \
    'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y' \
    'CONFIG_FREERTOS_HZ=1000' \
    'CONFIG_LV_COLOR_DEPTH_16=y' \
    'CONFIG_LV_DEF_REFR_PERIOD=8' \
    'CONFIG_LV_FONT_MONTSERRAT_10=y' \
    'CONFIG_LV_FONT_MONTSERRAT_16=y' \
    'CONFIG_LV_FONT_MONTSERRAT_18=y'; do
    if ! grep -q "^${setting}$" "${SDKCONFIG}" 2>/dev/null; then
        SDKCONFIG_READY=false
        break
    fi
done

if [[ "${SDKCONFIG_READY}" != true ]]; then
    if [[ -f "${SDKCONFIG}" ]]; then
        mv -f "${SDKCONFIG}" "${PROJECT_DIR}/firmware/sdkconfig.old"
    fi
    echo "Refreshing generated sdkconfig from sdkconfig.defaults"
    idf.py -C "${PROJECT_DIR}/firmware" set-target esp32s3
fi
if [[ "${SHOW_APP}" -eq 1 ]]; then
    idf.py \
        -C "${PROJECT_DIR}/firmware" \
        -D DOODAD_SHOW_APP_AT_BOOT=ON \
        -D DOODAD_BOOT_CATALOG_STORY=-1 \
        build
else
    idf.py \
        -C "${PROJECT_DIR}/firmware" \
        -D DOODAD_SHOW_APP_AT_BOOT=OFF \
        -D DOODAD_BOOT_CATALOG_STORY="${CATALOG_STORY}" \
        build
fi
