#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$SCRIPT_DIR/.ffmpeg/bin/ffmpeg" ]; then
    exec "$SCRIPT_DIR/.venv/bin/python" "$SCRIPT_DIR/server.py" \
        --ffmpeg "$SCRIPT_DIR/.ffmpeg/bin/ffmpeg" "$@"
fi
exec "$SCRIPT_DIR/.venv/bin/python" "$SCRIPT_DIR/server.py" "$@"
