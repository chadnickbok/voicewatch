#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$REPO_DIR/.." && pwd)
SERVICE_DIR="$REPO_DIR/services/live-agent"

set -a
if [ -f "$WORKSPACE_DIR/openai.env" ]; then
  . "$WORKSPACE_DIR/openai.env"
fi
if [ -f "$WORKSPACE_DIR/elevenlabs.env" ]; then
  . "$WORKSPACE_DIR/elevenlabs.env"
fi
set +a

exec "$SERVICE_DIR/.venv/bin/uv" run --directory "$SERVICE_DIR" --locked doodad-live-agent "$@"
