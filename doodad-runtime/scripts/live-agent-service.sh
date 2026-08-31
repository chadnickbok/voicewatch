#!/bin/sh
set -eu
umask 077

LABEL=dev.doodad.live-agent
SERVICE_MODE=webrtc
if [ "${1:-}" = --moq ]; then
  SERVICE_MODE=moq
  LABEL=dev.doodad.live-agent.moq
  shift
fi
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$REPO_DIR/.." && pwd)
SUPPORT_DIR="$HOME/Library/Application Support/Doodad"
if [ "$SERVICE_MODE" = moq ]; then
  SUPPORT_DIR="$SUPPORT_DIR/moq"
fi
DEPLOY_ROOT="$SUPPORT_DIR/runtime"
RUNNER="$DEPLOY_ROOT/scripts/run-live-agent.sh"
DOMAIN="gui/$(id -u)"
TARGET="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/Doodad"
if [ "$SERVICE_MODE" = moq ]; then
  LOG_DIR="$LOG_DIR/moq"
fi
MOQ_PROFILE="$SUPPORT_DIR/supervisor.json"

usage() {
  echo "usage: $0 [--moq] install|start|restart|stop|status|uninstall [private-moq-profile-for-install]" >&2
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
  if [ "$SERVICE_MODE" = moq ]; then
    "$REPO_DIR/services/live-agent/.venv/bin/uv" sync \
      --directory "$DEPLOY_ROOT/services/live-agent" --no-dev --locked
    "$REPO_DIR/services/live-agent/.venv/bin/python" -m doodad_agent.moq_deploy \
      --profile "$SOURCE_MOQ_PROFILE" --output "$MOQ_PROFILE" \
      --licenses "$WORKSPACE_DIR/libs/moq-esp32/server/voice_agent/licenses" --wait-unlocked 40
  else
    "$REPO_DIR/services/live-agent/.venv/bin/uv" sync \
      --directory "$DEPLOY_ROOT/services/live-agent" --extra webrtc --locked
  fi
}

write_plist() {
  mkdir -p "$(dirname "$TARGET")" "$LOG_DIR"
  temporary=$(mktemp "${TMPDIR:-/tmp}/$LABEL.XXXXXX.plist")
  trap 'rm -f "$temporary"' EXIT HUP INT TERM
  plutil -create xml1 "$temporary"
  plutil -insert Label -string "$LABEL" "$temporary"
  plutil -insert ProgramArguments -array "$temporary"
  plutil -insert ProgramArguments.0 -string "$RUNNER" "$temporary"
  if [ "$SERVICE_MODE" = moq ]; then
    plutil -insert ProgramArguments.1 -string supervise-moq "$temporary"
    plutil -insert ProgramArguments.2 -string --config "$temporary"
    plutil -insert ProgramArguments.3 -string "$MOQ_PROFILE" "$temporary"
  else
    plutil -insert ProgramArguments.1 -string serve "$temporary"
  fi
  plutil -insert WorkingDirectory -string "$DEPLOY_ROOT" "$temporary"
  plutil -insert RunAtLoad -bool true "$temporary"
  plutil -insert KeepAlive -bool true "$temporary"
  plutil -insert ProcessType -string Interactive "$temporary"
  plutil -insert ThrottleInterval -integer 10 "$temporary"
  plutil -insert ExitTimeOut -integer 40 "$temporary"
  plutil -insert Umask -integer 63 "$temporary"
  plutil -insert StandardOutPath -string "$LOG_DIR/live-agent.stdout.log" "$temporary"
  plutil -insert StandardErrorPath -string "$LOG_DIR/live-agent.stderr.log" "$temporary"
  install -m 600 "$temporary" "$TARGET"
  rm -f "$temporary"
  trap - EXIT HUP INT TERM
}

case ${1:-} in
  install)
    if [ "$SERVICE_MODE" = moq ]; then
      [ "$#" -eq 2 ] || usage
      SOURCE_MOQ_PROFILE=$2
      "$SCRIPT_DIR/run-live-agent.sh" supervise-moq --config "$SOURCE_MOQ_PROFILE" --check
    else
      [ "$#" -eq 1 ] || usage
    fi
    "$SCRIPT_DIR/run-live-agent.sh" check-config >/dev/null
    if loaded; then
      launchctl bootout "$DOMAIN/$LABEL"
    fi
    if [ "$SERVICE_MODE" = moq ]; then
      "$REPO_DIR/services/live-agent/.venv/bin/python" -m doodad_agent.moq_deploy \
        --wait-stopped "$MOQ_PROFILE" --wait-unlocked 40
    fi
    deploy_runtime
    "$RUNNER" check-config >/dev/null
    write_plist
    launchctl bootstrap "$DOMAIN" "$TARGET"
    if [ "$SERVICE_MODE" != moq ]; then
      launchctl kickstart -k "$DOMAIN/$LABEL"
    fi
    echo "Installed and started $LABEL"
    ;;
  start)
    if [ ! -f "$TARGET" ]; then
      echo "$TARGET is not installed; run '$0 install' first" >&2
      exit 2
    fi
    if ! loaded; then
      launchctl bootstrap "$DOMAIN" "$TARGET"
      if [ "$SERVICE_MODE" = moq ]; then
        exit 0
      fi
    fi
    launchctl kickstart -k "$DOMAIN/$LABEL"
    ;;
  restart)
    if [ "$SERVICE_MODE" = moq ]; then
      "$0" --moq stop
      "$0" --moq start
    else
      "$0" stop
      "$0" start
    fi
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
