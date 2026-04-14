#!/bin/bash
# Launch QLC+ with a workspace file, wait for MCP server to be ready.
# Usage: ./scripts/launch.sh [workspace.qxw]
# Defaults to proj1.qxw if no workspace specified.
# Kills any existing instance first.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd "$CLIENT_DIR/../.." && pwd)"

WORKSPACE="${1:-$REPO_DIR/proj1.qxw}"
LOG="/tmp/qlcplus-test.log"

# Source .env if present (credentials, not committed)
if [ -f "$CLIENT_DIR/.env" ]; then
    set -a
    source "$CLIENT_DIR/.env"
    set +a
fi

# Kill any existing instance
"$SCRIPT_DIR/kill.sh"

# Check binary exists
if [ ! -f "$CLIENT_DIR/build-v5/qmlui/qlcplus-qml.app/Contents/MacOS/qlcplus-qml" ]; then
    echo "ERROR: Binary not found. Run ./build.sh first."
    exit 1
fi

# Launch with test env vars:
# - AGENT_API_TOKEN: bypasses macOS keychain dialog
# - GDTF_SHARE_USER/PASSWORD: passed through for auto-login if set
cd "$CLIENT_DIR"
AGENT_API_TOKEN=dev-token-change-me \
    GDTF_SHARE_USER="${GDTF_SHARE_USER:-}" \
    GDTF_SHARE_PASSWORD="${GDTF_SHARE_PASSWORD:-}" \
    ./run.sh "$WORKSPACE" &>"$LOG" &
QLC_PID=$!
echo "QLC+ PID: $QLC_PID (log: $LOG)"

# Wait for MCP server (up to 15 seconds)
for i in $(seq 1 15); do
    if grep -q "MCP server started" "$LOG" 2>/dev/null; then
        echo "MCP ready on port 9876"
        exit 0
    fi
    sleep 1
done

echo "ERROR: MCP server did not start within 15s"
tail -20 "$LOG"
exit 1
