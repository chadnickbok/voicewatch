#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="$ROOT/src/doodad_agent/codex_protocol_schemas"
EXPECTED="codex-cli 0.146.0-alpha.9.2"
CODEX_BIN="${DOODAD_CODEX_BINARY:-codex}"

ACTUAL="$($CODEX_BIN --version)"
if [[ "$ACTUAL" != "$EXPECTED" ]]; then
  echo "expected $EXPECTED, got $ACTUAL" >&2
  exit 1
fi

TEMP="$(mktemp -d /tmp/doodad-codex-protocol.XXXXXX)"
trap 'rm -rf "$TEMP"' EXIT
"$CODEX_BIN" app-server generate-json-schema --experimental --out "$TEMP"

check_schema() {
  local relative="$1"
  local expected="$2"
  local actual
  actual="$(shasum -a 256 "$TEMP/$relative" | cut -d' ' -f1)"
  if [[ "$actual" != "$expected" ]]; then
    echo "$relative hash mismatch: expected $expected, got $actual" >&2
    exit 1
  fi
}

check_schema "v1/InitializeParams.json" "4f576f99e285beb28f71f48a72b887c1f517dada86fee348fe2af0a35511de23"
check_schema "v2/ThreadStartParams.json" "b3685411ceb8ad264a1920e8facd66301e5280948ef9c2a6871b95d4c19da639"
check_schema "v2/ThreadResumeParams.json" "2e1d4b62bc09b46ebc54ef9f84fcdd6ca8d37cabb98dedc34b49761ee764c84d"
check_schema "v2/TurnStartParams.json" "f23021c02d28b60fccb6dcaaace9ff676127065f8254537265d6622656860dca"
check_schema "v2/TurnSteerParams.json" "802b236f03d4a691c3bfc6d2e8b76a3592dab1f7593ac6e520aed762fb397898"
check_schema "v2/TurnInterruptParams.json" "49132b57b09f09dc545ed1cd373c12eede6e880e9afb54ae50add78bb42490cd"
check_schema "ToolRequestUserInputParams.json" "21e569e32c05d51c1ee5e587730c182b911ede97a4df267f6e4ef24e1717f34e"
check_schema "ToolRequestUserInputResponse.json" "14ede53c2e51b289fb3c80903292d4b0f0b387eae217dbb257c201b2b7c65bf1"

mkdir -p "$OUTPUT/v1" "$OUTPUT/v2"
cp "$TEMP/v1/InitializeParams.json" "$OUTPUT/v1/InitializeParams.json"
cp "$TEMP/v2/ThreadStartParams.json" "$OUTPUT/v2/ThreadStartParams.json"
cp "$TEMP/v2/ThreadResumeParams.json" "$OUTPUT/v2/ThreadResumeParams.json"
cp "$TEMP/v2/TurnStartParams.json" "$OUTPUT/v2/TurnStartParams.json"
cp "$TEMP/v2/TurnSteerParams.json" "$OUTPUT/v2/TurnSteerParams.json"
cp "$TEMP/v2/TurnInterruptParams.json" "$OUTPUT/v2/TurnInterruptParams.json"
cp "$TEMP/ToolRequestUserInputParams.json" "$OUTPUT/ToolRequestUserInputParams.json"
cp "$TEMP/ToolRequestUserInputResponse.json" "$OUTPUT/ToolRequestUserInputResponse.json"

echo "generated pinned Codex app-server schemas in $OUTPUT"
