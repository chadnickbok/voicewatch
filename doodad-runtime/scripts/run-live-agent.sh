#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$REPO_DIR/.." && pwd)
SERVICE_DIR="$REPO_DIR/services/live-agent"

# launchd starts with a deliberately small PATH. Keep the pinned project tools
# discoverable when the agent is running without an open terminal.
PATH="$HOME/.asdf/shims:$HOME/.asdf/bin:/opt/homebrew/bin:/usr/local/bin:$PATH"
export PATH

set -a
if [ -f "$WORKSPACE_DIR/openai.env" ]; then
  . "$WORKSPACE_DIR/openai.env"
fi
if [ -f "$WORKSPACE_DIR/elevenlabs.env" ]; then
  . "$WORKSPACE_DIR/elevenlabs.env"
fi
set +a

# Retrieve the personal signing profile from macOS Keychain when it was not
# supplied explicitly. The key is never printed or persisted by this script.
if [ -z "${DOODAD_PERSONAL_HMAC_KEY_HEX:-}" ] && command -v security >/dev/null 2>&1; then
  personal_owner=${DOODAD_PERSONAL_OWNER_ID:-local.nick}
  personal_keychain_service=${DOODAD_PERSONAL_KEYCHAIN_SERVICE:-voicewatch.doodad.personal.hmac}
  if personal_hmac_key=$(security find-generic-password \
      -a "$personal_owner" \
      -s "$personal_keychain_service" \
      -w 2>/dev/null); then
    DOODAD_PERSONAL_OWNER_ID=$personal_owner
    DOODAD_PERSONAL_SIGNER_KEY_ID=${DOODAD_PERSONAL_SIGNER_KEY_ID:-personal-v1}
    DOODAD_PERSONAL_HMAC_KEY_HEX=$personal_hmac_key
    export DOODAD_PERSONAL_OWNER_ID DOODAD_PERSONAL_SIGNER_KEY_ID
    export DOODAD_PERSONAL_HMAC_KEY_HEX
  fi
  unset personal_hmac_key
fi

if [ -x "$SERVICE_DIR/.venv/bin/doodad-live-agent" ]; then
  exec "$SERVICE_DIR/.venv/bin/doodad-live-agent" "$@"
fi

echo "Live-agent environment is missing; run uv sync in $SERVICE_DIR" >&2
exit 2
