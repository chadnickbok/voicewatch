#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALL_DIR="$SCRIPT_DIR/.ffmpeg"
FFMPEG_COMMIT=ddf8f40301af20ad985cf369d1eb6d114be0c8f0

if [ -x "$INSTALL_DIR/bin/ffmpeg" ]; then
    exit 0
fi

if [ "$(uname -s)" != Darwin ]; then
    echo "The pinned capture FFmpeg build is only required on macOS." >&2
    exit 2
fi

BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/doodad-ffmpeg.XXXXXX")
cleanup() {
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT INT TERM

echo "Building FFmpeg $FFMPEG_COMMIT with the AVFoundation frame-loss fix"
curl -L --fail --silent --show-error \
    "https://github.com/FFmpeg/FFmpeg/archive/$FFMPEG_COMMIT.tar.gz" \
    | tar -xz -C "$BUILD_DIR" --strip-components=1

mkdir -p "$INSTALL_DIR"
cd "$BUILD_DIR"
./configure \
    --prefix="$INSTALL_DIR" \
    --disable-debug \
    --disable-doc \
    --disable-ffplay \
    --disable-ffprobe

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
make -j "$JOBS"
make install

"$INSTALL_DIR/bin/ffmpeg" -version | head -n 1
