#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORT=""
BOARD="cores3"
OPEN_MONITOR=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="${2:?--port requires a serial device}"; shift 2 ;;
        --board)
            BOARD="${2:?--board requires cores3, t-watch-s3, or t-watch-ultra}"
            [[ "${BOARD}" == "cores3" || "${BOARD}" == "t-watch-s3" || "${BOARD}" == "t-watch-ultra" ]] || {
                echo "Unknown board: ${BOARD}" >&2; exit 2;
            }
            shift 2
            ;;
        --no-monitor) OPEN_MONITOR=0; shift ;;
        *)
            echo "Usage: $0 --board cores3|t-watch-s3|t-watch-ultra [--port DEVICE] [--no-monitor]" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${PORT}" ]]; then
    ports=(/dev/cu.usbmodem*)
    if [[ "${ports[0]}" == '/dev/cu.usbmodem*' ]]; then
        echo "No USB serial device found. Supply one with --port." >&2
        exit 1
    fi
    if [[ "${#ports[@]}" -ne 1 ]]; then
        echo "Multiple USB serial devices are attached; --port is required:" >&2
        printf '  %s\n' "${ports[@]}" >&2
        exit 1
    fi
    PORT="${ports[0]}"
fi
if [[ ! -e "${PORT}" ]]; then
    echo "Serial device does not exist: ${PORT}" >&2
    exit 1
fi
BUILD_DIR="${PROJECT_DIR}/firmware/build/${BOARD}"
if [[ ! -f "${BUILD_DIR}/flash_args" ]]; then
    echo "Missing ${BOARD} build. Run scripts/build-firmware.sh --board ${BOARD}." >&2
    exit 1
fi

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/env.sh"
if [[ "${BOARD}" == "t-watch-ultra" ]]; then
    # Ultra bring-up preserves the inspected data/OTA layout. The runner checks
    # live security/layout, writes app0 only, then verifies a shell heartbeat.
    # No default-firmware backup or restoration is required.
    output_dir="$(mktemp -d "${TMPDIR:-/tmp}/voicewatch-ultra-flash.XXXXXX")"
    python "${PROJECT_DIR}/../libs/moq-esp32/tools/run_ultra_transport.py" \
        --image "${BUILD_DIR}/doodad_runtime.bin" --port "${PORT}" \
        --output-dir "${output_dir}" --timeout 150 \
        --success-marker '[host] uptime heartbeat; free heap:'
    echo "Private Ultra flash/heartbeat evidence: ${output_dir}"
else
    idf.py -C "${PROJECT_DIR}/firmware" -B "${BUILD_DIR}" -p "${PORT}" flash
fi
if [[ "${OPEN_MONITOR}" -eq 1 ]]; then
    idf.py -C "${PROJECT_DIR}/firmware" -B "${BUILD_DIR}" -p "${PORT}" monitor
fi
