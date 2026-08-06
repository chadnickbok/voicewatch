#!/bin/sh
set -eu

LABEL=dev.doodad.live-agent
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$REPO_DIR/.." && pwd)
SUPPORT_DIR="$HOME/Library/Application Support/Doodad"
DEPLOY_ROOT="$SUPPORT_DIR/runtime"
RUNNER="$DEPLOY_ROOT/scripts/run-live-agent.sh"
DOMAIN="gui/$(id -u)"
TARGET="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/Doodad"

usage() {
  echo "usage: $0 install|start|restart|stop|status|uninstall" >&2
  exit 2
}

loaded() {
  launchctl print "$DOMAIN/$LABEL" >/dev/null 2>&1
}

deploy_runtime() {
  for credentials in openai.env elevenlabs.env; do
    if [ ! -f "$WORKSPACE_DIR/$credentials" ]; then
      echo "Missing $WORKSPACE_DIR/$credentials" >&2
      exit 2
    fi
  done
  mkdir -p "$DEPLOY_ROOT" "$SUPPORT_DIR"
  rsync -a --delete \
    --exclude .git/ \
    --exclude evidence/ \
    --exclude firmware/build/ \
    --exclude 'firmware/build-*/' \
    --exclude services/live-agent/.venv/ \
    --exclude services/live-agent/evidence/ \
    --exclude target/ \
    --exclude __pycache__/ \
    --exclude .pytest_cache/ \
    --exclude tools/voice-uplink/models/ \
    --exclude tools/voice-uplink/artifacts/ \
    "$REPO_DIR/" "$DEPLOY_ROOT/"
  install -m 600 "$WORKSPACE_DIR/openai.env" "$SUPPORT_DIR/openai.env"
  install -m 600 "$WORKSPACE_DIR/elevenlabs.env" "$SUPPORT_DIR/elevenlabs.env"
  "$REPO_DIR/services/live-agent/.venv/bin/uv" sync \
    --directory "$DEPLOY_ROOT/services/live-agent" --locked
}

write_plist() {
  mkdir -p "$(dirname "$TARGET")" "$LOG_DIR"
  temporary=$(mktemp "${TMPDIR:-/tmp}/$LABEL.XXXXXX.plist")
  trap 'rm -f "$temporary"' EXIT HUP INT TERM
  plutil -create xml1 "$temporary"
  plutil -insert Label -string "$LABEL" "$temporary"
  plutil -insert ProgramArguments -json "[\"$RUNNER\",\"serve\"]" "$temporary"
  plutil -insert WorkingDirectory -string "$DEPLOY_ROOT" "$temporary"
  plutil -insert RunAtLoad -bool true "$temporary"
  plutil -insert KeepAlive -bool true "$temporary"
  plutil -insert ProcessType -string Interactive "$temporary"
  plutil -insert ThrottleInterval -integer 10 "$temporary"
  plutil -insert StandardOutPath -string "$LOG_DIR/live-agent.stdout.log" "$temporary"
  plutil -insert StandardErrorPath -string "$LOG_DIR/live-agent.stderr.log" "$temporary"
  install -m 600 "$temporary" "$TARGET"
  rm -f "$temporary"
  trap - EXIT HUP INT TERM
}

case ${1:-} in
  install)
    "$SCRIPT_DIR/run-live-agent.sh" check-config >/dev/null
    if loaded; then
      launchctl bootout "$DOMAIN/$LABEL"
    fi
    deploy_runtime
    "$RUNNER" check-config >/dev/null
    write_plist
    launchctl bootstrap "$DOMAIN" "$TARGET"
    launchctl kickstart -k "$DOMAIN/$LABEL"
    echo "Installed and started $LABEL"
    ;;
  start)
    if [ ! -f "$TARGET" ]; then
      echo "$TARGET is not installed; run '$0 install' first" >&2
      exit 2
    fi
    if ! loaded; then
      launchctl bootstrap "$DOMAIN" "$TARGET"
    fi
    launchctl kickstart -k "$DOMAIN/$LABEL"
    ;;
  restart)
    "$0" stop
    "$0" start
    ;;
  stop)
    if loaded; then
      launchctl bootout "$DOMAIN/$LABEL"
    fi
    ;;
  status)
    if loaded; then
      launchctl print "$DOMAIN/$LABEL"
    else
      echo "$LABEL is not loaded" >&2
      exit 1
    fi
    ;;
  uninstall)
    if loaded; then
      launchctl bootout "$DOMAIN/$LABEL"
    fi
    if [ -f "$TARGET" ]; then
      rm -f "$TARGET"
    fi
    echo "Uninstalled $LABEL"
    ;;
  *) usage ;;
esac
