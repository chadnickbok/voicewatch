#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SERVICE_DIR="$REPO_DIR/services/live-agent"

exec "$SERVICE_DIR/.venv/bin/uv" run --directory "$SERVICE_DIR" --all-groups --locked pytest
