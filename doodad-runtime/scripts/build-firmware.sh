#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

"${SCRIPT_DIR}/build-guest.sh"

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
idf.py -C "${PROJECT_DIR}/firmware" build
