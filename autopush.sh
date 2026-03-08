#!/usr/bin/env bash
# autopush.sh — watches pokefirered-native for changes and pushes to GitHub
# Run with: nohup ./autopush.sh &
# Stop with: kill $(cat /tmp/pfr_autopush.pid)

set -euo pipefail

REPO="/home/spark-advantage/pokefirered-native"
INTERVAL="${1:-30}"  # seconds between checks, default 30
PIDFILE="/tmp/pfr_autopush.pid"
LOGFILE="/tmp/pfr_autopush.log"
GH="$HOME/.local/bin/gh"

# Ensure only one instance
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "Already running (PID $(cat "$PIDFILE")). Kill it first or remove $PIDFILE."
    exit 1
fi
echo $$ > "$PIDFILE"
trap 'rm -f "$PIDFILE"; exit' EXIT INT TERM

log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOGFILE"; }

log "autopush started (PID $$, interval ${INTERVAL}s)"

while true; do
    cd "$REPO"

    # Check for any changes (staged, unstaged, or untracked)
    if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
        # Stage everything except build artifacts (gitignore handles this)
        git add -A

        # Build a commit message from the changed files
        CHANGED=$(git diff --cached --stat | tail -1)
        TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

        git commit -m "$(cat <<EOF
auto: sync ${TIMESTAMP}

${CHANGED}
EOF
)" >> "$LOGFILE" 2>&1

        if git push >> "$LOGFILE" 2>&1; then
            log "pushed: ${CHANGED}"
        else
            log "push failed — will retry next cycle"
        fi
    fi

    sleep "$INTERVAL"
done
