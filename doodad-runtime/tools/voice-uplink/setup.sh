#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PYTHON_BIN=${PYTHON_BIN:-python3.12}
MODEL_DIR="$SCRIPT_DIR/models"
MODEL_PATH="$MODEL_DIR/ggml-small.en.bin"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin"

if ! command -v whisper-cli >/dev/null 2>&1; then
    brew install whisper-cpp
fi

"$PYTHON_BIN" -m venv "$SCRIPT_DIR/.venv"
"$SCRIPT_DIR/.venv/bin/python" -m pip install --upgrade pip
"$SCRIPT_DIR/.venv/bin/python" -m pip install -r "$SCRIPT_DIR/requirements.txt"

mkdir -p "$MODEL_DIR"
if [ ! -f "$MODEL_PATH" ]; then
    curl --fail --location --output "$MODEL_PATH" "$MODEL_URL"
fi

echo "Echo Bridge dependencies are ready."
