#!/bin/zsh
set -euo pipefail

script_dir="${0:A:h}"
project_dir="${script_dir:h}"
app_dir="${project_dir}/.build/CleanCam.app"
contents_dir="${app_dir}/Contents"

cd "${project_dir}"
swift build -c release

mkdir -p "${contents_dir}/MacOS" "${contents_dir}/Resources"
cp "${project_dir}/.build/release/CleanCam" "${contents_dir}/MacOS/CleanCam"
cp "${project_dir}/App/Info.plist" "${contents_dir}/Info.plist"
codesign --force --deep --sign - "${app_dir}"

echo "${app_dir}"
