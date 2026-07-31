#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPOSITORY_ROOT="$(cd "${PROJECT_DIR}/../.." && pwd)"
PACKAGE="dev.doodad.reference"
PROFILE="watch_square_240"
SERIAL=""
APP=""
OUTPUT_DIR="${REPOSITORY_ROOT}/target/parallax/runtime-wear-square-240"

usage() {
    echo "Usage: $0 [--serial SERIAL] [--app SLUG] [--output DIR]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            SERIAL="${2:?--serial requires a value}"
            shift 2
            ;;
        --app)
            APP="${2:?--app requires a value}"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="${2:?--output requires a value}"
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
"${SCRIPT_DIR}/configure-square-runtime.sh" --serial "${SERIAL}" >/dev/null

sdk="$(adb "${adb_args[@]}" shell getprop ro.build.version.sdk | tr -d '\r')"
avd_name="$(
    adb "${adb_args[@]}" shell getprop ro.boot.qemu.avd_name | tr -d '\r'
)"
size="$(adb "${adb_args[@]}" shell wm size | tr -d '\r')"
density="$(adb "${adb_args[@]}" shell wm density | tr -d '\r')"
fingerprint="$(
    adb "${adb_args[@]}" shell getprop ro.build.fingerprint | tr -d '\r'
)"
product_abi="$(
    adb "${adb_args[@]}" shell getprop ro.product.cpu.abi | tr -d '\r'
)"

if [[ "${sdk}" != "37" ]]; then
    echo "Expected API 37, got API ${sdk} on ${SERIAL}." >&2
    exit 1
fi
if [[ "${size}" != *"Override size: 240x240"* ]]; then
    echo "Expected a 240x240 runtime, got: ${size}" >&2
    exit 1
fi
if [[ "${density}" != *"200"* ]]; then
    echo "Expected Android density 200, got: ${density}" >&2
    exit 1
fi

system_image_package="system-images;android-37.0;android-wear-signed;${product_abi}"
system_image_xml="$(
    printf '%s/system-images/android-37.0/android-wear-signed/%s/package.xml' \
        "${ANDROID_HOME}" \
        "${product_abi}"
)"
emulator_package_xml="${ANDROID_HOME}/emulator/package.xml"
read_package_revision() {
    python3 - "$1" <<'PY'
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

path = Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(f"Android SDK package metadata not found: {path}")
root = ET.parse(path).getroot()
local_package = next(
    element
    for element in root.iter()
    if element.tag.rsplit("}", 1)[-1] == "localPackage"
)
revision = next(
    element
    for element in local_package
    if element.tag.rsplit("}", 1)[-1] == "revision"
)
parts = {
    element.tag.rsplit("}", 1)[-1]: element.text
    for element in revision
}
version = [parts["major"]]
if "minor" in parts:
    version.append(parts["minor"])
if "micro" in parts:
    version.append(parts["micro"])
print(".".join(version))
PY
}
system_image_revision="$(read_package_revision "${system_image_xml}")"
emulator_revision="$(read_package_revision "${emulator_package_xml}")"

"${SCRIPT_DIR}/install-debug.sh" --serial "${SERIAL}"
apk_path="${PROJECT_DIR}/app/build/outputs/apk/debug/app-debug.apk"
apk_sha256="$(shasum -a 256 "${apk_path}" | awk '{print $1}')"
mkdir -p "${OUTPUT_DIR}"

selection_args=()
if [[ -n "${APP}" ]]; then
    selection_args=(--app "${APP}")
fi

while IFS=$'\t' read -r slug asset_path snapshot_sha256; do
    base="${OUTPUT_DIR}/${slug}.resting.${PROFILE}"
    adb "${adb_args[@]}" shell am start -S -W \
        -n "${PACKAGE}/.MainActivity" \
        --es parallax_snapshot_asset "${asset_path}" \
        --es parallax_profile "${PROFILE}" \
        </dev/null >/dev/null
    sleep 1

    adb "${adb_args[@]}" exec-out screencap -p \
        </dev/null >"${base}.png"
    remote_xml="/data/local/tmp/${slug}.parallax.xml"
    adb "${adb_args[@]}" shell uiautomator dump "${remote_xml}" \
        </dev/null >/dev/null
    adb "${adb_args[@]}" exec-out cat "${remote_xml}" \
        </dev/null >"${base}.semantics.xml"
    adb "${adb_args[@]}" shell rm "${remote_xml}" </dev/null

    python3 - \
        "${base}" \
        "${slug}" \
        "${snapshot_sha256}" \
        "${SERIAL}" \
        "${avd_name}" \
        "${sdk}" \
        "${size}" \
        "${density}" \
        "${fingerprint}" \
        "${product_abi}" \
        "${system_image_package}" \
        "${system_image_revision}" \
        "${emulator_revision}" \
        "${apk_sha256}" <<'PY'
import hashlib
import json
from pathlib import Path
import struct
import sys

(
    base,
    slug,
    snapshot_sha256,
    serial,
    avd_name,
    sdk,
    size,
    density,
    fingerprint,
    product_abi,
    system_image_package,
    system_image_revision,
    emulator_revision,
    apk_sha256,
) = sys.argv[1:]
base_path = Path(base)
png_path = base_path.with_suffix(".watch_square_240.png")
xml_path = base_path.with_suffix(".watch_square_240.semantics.xml")

payload = png_path.read_bytes()
if not payload.startswith(b"\x89PNG\r\n\x1a\n"):
    raise SystemExit(f"Runtime capture is not a PNG: {png_path}")
width, height = struct.unpack(">II", payload[16:24])
if (width, height) != (240, 240):
    raise SystemExit(
        f"Runtime capture is {width}x{height}, expected 240x240"
    )

def artifact(path: Path) -> dict:
    content = path.read_bytes()
    return {
        "path": path.name,
        "bytes": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    }

manifest = {
    "schema_version": 1,
    "kind": "parallax-compose-runtime-capture",
    "selection": {
        "app_slug": slug,
        "snapshot_sha256": snapshot_sha256,
        "capture_phase": "resting",
        "profile_id": "watch_square_240",
    },
    "renderer": {
        "kind": "compose",
        "mode": "emulator",
        "version": "wear-compose-1.6.2",
        "build_sha256": apk_sha256,
    },
    "device": {
        "serial": serial,
        "avd_name": avd_name,
        "api": int(sdk),
        "wm_size": size,
        "wm_density": density,
        "build_fingerprint": fingerprint,
        "product_abi": product_abi,
        "system_image_package": system_image_package,
        "system_image_revision": system_image_revision,
        "emulator_revision": emulator_revision,
    },
    "framebuffer": {
        "format": "png",
        "physical_width_px": width,
        "physical_height_px": height,
        "logical_width_dp": 192,
        "logical_height_dp": 192,
        "density": 1.25,
    },
    "artifacts": {
        "screenshot": artifact(png_path),
        "accessibility_xml": artifact(xml_path),
    },
}
manifest_path = base_path.with_suffix(
    ".watch_square_240.runtime-manifest.json"
)
manifest_path.write_text(
    json.dumps(
        manifest,
        ensure_ascii=False,
        allow_nan=False,
        indent=2,
        sort_keys=True,
    )
    + "\n"
)
PY
done < <(
    PYTHONPATH="${REPOSITORY_ROOT}/tools" python3 - \
        "${REPOSITORY_ROOT}" \
        "${APP}" <<'PY'
from pathlib import Path
import sys

from doodad_cli.perfect_render import resolve_suite_entries

root = Path(sys.argv[1])
app = sys.argv[2] or None
selections = resolve_suite_entries(
    root / "reference" / "perfect-render-suite.json",
    app_slug=app,
)
for selection in selections:
    snapshot = selection.target_entry["snapshot"]
    asset = (
        f"{selection.entry['app_slug']}/decisive/{snapshot['path']}"
    )
    print(
        selection.entry["app_slug"],
        asset,
        selection.entry["snapshot_sha256"],
        sep="\t",
    )
PY
)

echo "Captured API 37 square runtime evidence:"
echo "${OUTPUT_DIR}"
