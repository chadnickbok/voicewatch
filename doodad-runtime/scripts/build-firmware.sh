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

# Existing per-board sdkconfigs are intentionally ignored because they can
# contain local Wi-Fi and personal signing secrets. Migrate the FAT choice in
# place rather than regenerating the file and dropping those values when LFN
# support was added.
migrate_fatfs_lfn() {
    [[ -f "${SDKCONFIG}" ]] || return 0
    if grep -q '^CONFIG_FATFS_LFN_HEAP=y$' "${SDKCONFIG}" \
        && grep -q '^CONFIG_FATFS_MAX_LFN=255$' "${SDKCONFIG}"; then
        return 0
    fi

    local migrated
    migrated="$(mktemp "${SDKCONFIG}.lfn.XXXXXX")"
    if ! awk '
        BEGIN { heap = 0; maximum = 0 }
        /^CONFIG_FATFS_LFN_NONE=y$/ {
            print "# CONFIG_FATFS_LFN_NONE is not set"
            next
        }
        /^CONFIG_FATFS_LFN_STACK=y$/ {
            print "# CONFIG_FATFS_LFN_STACK is not set"
            next
        }
        /^# CONFIG_FATFS_LFN_HEAP is not set$/ {
            print "CONFIG_FATFS_LFN_HEAP=y"
            heap = 1
            next
        }
        /^CONFIG_FATFS_LFN_HEAP=y$/ { heap = 1 }
        /^CONFIG_FATFS_MAX_LFN=/ {
            print "CONFIG_FATFS_MAX_LFN=255"
            maximum = 1
            next
        }
        { print }
        END {
            if (!heap) print "CONFIG_FATFS_LFN_HEAP=y"
            if (!maximum) print "CONFIG_FATFS_MAX_LFN=255"
        }
    ' "${SDKCONFIG}" > "${migrated}"; then
        rm -f "${migrated}"
        return 1
    fi
    chmod 600 "${migrated}"
    mv -f "${migrated}" "${SDKCONFIG}"
    echo "Migrated ${BOARD} sdkconfig to FATFS long filenames"
}

migrate_fatfs_lfn
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
    'CONFIG_FATFS_LFN_HEAP=y' \
    'CONFIG_FATFS_MAX_LFN=255' \
    'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y' \
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

    # `set-target` regenerates sdkconfig from tracked defaults. Restore only
    # the ignored device-local network/trust fields from the saved config so a
    # required default migration cannot silently deprovision the watch.
    if [[ -f "${SDKCONFIG}.old" ]]; then
        local_config_tmp="$(mktemp "${SDKCONFIG}.local.XXXXXX")"
        awk '
            NR == FNR {
                if ($0 ~ /^CONFIG_DOODAD_(WIFI_SSID|WIFI_PASSWORD|VOICE_SIGNALING_URL|PERSONAL_OWNER_ID|PERSONAL_SIGNER_KEY_ID|PERSONAL_HMAC_KEY_HEX)=/) {
                    split($0, fields, "=")
                    saved[fields[1]] = $0
                }
                next
            }
            $0 ~ /^CONFIG_DOODAD_(WIFI_SSID|WIFI_PASSWORD|VOICE_SIGNALING_URL|PERSONAL_OWNER_ID|PERSONAL_SIGNER_KEY_ID|PERSONAL_HMAC_KEY_HEX)=/ {
                split($0, fields, "=")
                if (fields[1] in saved) {
                    print saved[fields[1]]
                    next
                }
            }
            { print }
        ' "${SDKCONFIG}.old" "${SDKCONFIG}" > "${local_config_tmp}"
        chmod 600 "${local_config_tmp}"
        mv -f "${local_config_tmp}" "${SDKCONFIG}"
        echo "Restored ignored local network/trust profile for ${BOARD}"
    fi
fi

# Recover configs produced by the older refresh path, which saved the local
# profile but left the regenerated Wi-Fi fields empty.
if [[ -f "${SDKCONFIG}.old" ]] \
    && grep -q '^CONFIG_DOODAD_WIFI_SSID=""$' "${SDKCONFIG}" \
    && ! grep -q '^CONFIG_DOODAD_WIFI_SSID=""$' "${SDKCONFIG}.old"; then
    recovery_tmp="$(mktemp "${SDKCONFIG}.recovery.XXXXXX")"
    awk '
        NR == FNR {
            if ($0 ~ /^CONFIG_DOODAD_(WIFI_SSID|WIFI_PASSWORD)=/) {
                split($0, fields, "=")
                saved[fields[1]] = $0
            }
            next
        }
        $0 ~ /^CONFIG_DOODAD_(WIFI_SSID|WIFI_PASSWORD)=/ {
            split($0, fields, "=")
            if (fields[1] in saved) {
                print saved[fields[1]]
                next
            }
        }
        { print }
    ' "${SDKCONFIG}.old" "${SDKCONFIG}" > "${recovery_tmp}"
    chmod 600 "${recovery_tmp}"
    mv -f "${recovery_tmp}" "${SDKCONFIG}"
    echo "Recovered ignored local Wi-Fi profile for ${BOARD}"
fi

# Optional local trust/signaling profile. The per-board sdkconfig is ignored by
# git; values are never printed. Supplying only part of the trust tuple fails
# closed so a build cannot silently produce install-disabled firmware.
if [[ -n "${DOODAD_PERSONAL_OWNER_ID:-}" ||
      -n "${DOODAD_PERSONAL_HMAC_KEY_HEX:-}" ]]; then
    [[ "${DOODAD_PERSONAL_OWNER_ID:-}" =~ ^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$ ]] || {
        echo "DOODAD_PERSONAL_OWNER_ID is invalid" >&2; exit 2;
    }
    [[ "${DOODAD_PERSONAL_HMAC_KEY_HEX:-}" =~ ^[0-9A-Fa-f]{64}$ ]] || {
        echo "DOODAD_PERSONAL_HMAC_KEY_HEX must contain 64 hex characters" >&2; exit 2;
    }
    local_signer="${DOODAD_PERSONAL_SIGNER_KEY_ID:-personal-v1}"
    [[ "${local_signer}" =~ ^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$ ]] || {
        echo "DOODAD_PERSONAL_SIGNER_KEY_ID is invalid" >&2; exit 2;
    }
    local_signal="${DOODAD_VOICE_SIGNALING_URL:-}"
    [[ "${local_signal}" != *'"'* && "${local_signal}" != *$'\n'* ]] || {
        echo "DOODAD_VOICE_SIGNALING_URL contains unsupported characters" >&2; exit 2;
    }
    profile_tmp="$(mktemp "${SDKCONFIG}.profile.XXXXXX")"
    awk -v owner="${DOODAD_PERSONAL_OWNER_ID}" \
        -v signer="${local_signer}" \
        -v key="${DOODAD_PERSONAL_HMAC_KEY_HEX}" \
        -v signal="${local_signal}" '
        BEGIN { o=0; s=0; k=0; u=0 }
        /^CONFIG_DOODAD_PERSONAL_OWNER_ID=/ { print "CONFIG_DOODAD_PERSONAL_OWNER_ID=\"" owner "\""; o=1; next }
        /^CONFIG_DOODAD_PERSONAL_SIGNER_KEY_ID=/ { print "CONFIG_DOODAD_PERSONAL_SIGNER_KEY_ID=\"" signer "\""; s=1; next }
        /^CONFIG_DOODAD_PERSONAL_HMAC_KEY_HEX=/ { print "CONFIG_DOODAD_PERSONAL_HMAC_KEY_HEX=\"" key "\""; k=1; next }
        /^CONFIG_DOODAD_VOICE_SIGNALING_URL=/ { print "CONFIG_DOODAD_VOICE_SIGNALING_URL=\"" signal "\""; u=1; next }
        { print }
        END {
            if (!o) print "CONFIG_DOODAD_PERSONAL_OWNER_ID=\"" owner "\""
            if (!s) print "CONFIG_DOODAD_PERSONAL_SIGNER_KEY_ID=\"" signer "\""
            if (!k) print "CONFIG_DOODAD_PERSONAL_HMAC_KEY_HEX=\"" key "\""
            if (!u) print "CONFIG_DOODAD_VOICE_SIGNALING_URL=\"" signal "\""
        }
    ' "${SDKCONFIG}" > "${profile_tmp}"
    chmod 600 "${profile_tmp}"
    mv -f "${profile_tmp}" "${SDKCONFIG}"
    echo "Applied ignored local personal-app profile for ${BOARD}"
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
