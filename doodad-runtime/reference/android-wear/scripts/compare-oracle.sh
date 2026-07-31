#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 COMPOSE_IMAGE LVGL_IMAGE OUTPUT_PREFIX" >&2
    exit 2
fi

COMPOSE_IMAGE="$1"
LVGL_IMAGE="$2"
OUTPUT_PREFIX="$3"

if ! command -v magick >/dev/null 2>&1; then
    echo "ImageMagick 7 is required for comparison output." >&2
    exit 1
fi

for image in "${COMPOSE_IMAGE}" "${LVGL_IMAGE}"; do
    if [[ ! -f "${image}" ]]; then
        echo "Missing image: ${image}" >&2
        exit 1
    fi
done

mkdir -p "$(dirname "${OUTPUT_PREFIX}")"

magick "${COMPOSE_IMAGE}" -resize '240x240!' \
    "${OUTPUT_PREFIX}.compose.png"
magick "${LVGL_IMAGE}" -resize '240x240!' \
    "${OUTPUT_PREFIX}.lvgl.png"
magick "${OUTPUT_PREFIX}.compose.png" "${OUTPUT_PREFIX}.lvgl.png" \
    -compose difference -composite \
    "${OUTPUT_PREFIX}.difference.png"
magick "${OUTPUT_PREFIX}.compose.png" "${OUTPUT_PREFIX}.lvgl.png" \
    -define compose:args=50 -compose blend -composite \
    "${OUTPUT_PREFIX}.overlay.png"
magick "${OUTPUT_PREFIX}.compose.png" \
    -colorspace Gray -canny 0x1+10%+30% \
    "${OUTPUT_PREFIX}.compose-boundaries.png"
magick "${OUTPUT_PREFIX}.lvgl.png" \
    -colorspace Gray -canny 0x1+10%+30% \
    "${OUTPUT_PREFIX}.lvgl-boundaries.png"

set +e
metric="$(
    magick compare -metric RMSE \
        "${OUTPUT_PREFIX}.compose.png" \
        "${OUTPUT_PREFIX}.lvgl.png" \
        null: 2>&1
)"
compare_exit=$?
set -e

printf 'rmse=%s\ncompare_exit=%s\n' "${metric}" "${compare_exit}" \
    >"${OUTPUT_PREFIX}.metrics.txt"
echo "Comparison written to ${OUTPUT_PREFIX}.*"
