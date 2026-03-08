#!/usr/bin/env bash
# autopush.sh — intelligent auto-commit/push daemon for pokefirered-native
#
# Watches for file changes AND reads GPT/Copilot's thinking notes to build
# meaningful commit messages. Acts as a lightweight progress tracker.
#
# Run with: nohup ./autopush.sh &
# Stop with: kill $(cat /tmp/pfr_autopush.pid)
# View log: tail -f /tmp/pfr_autopush.log

set -euo pipefail

REPO="/home/spark-advantage/pokefirered-native"
INTERVAL="${1:-30}"  # seconds between checks, default 30
PIDFILE="/tmp/pfr_autopush.pid"
LOGFILE="/tmp/pfr_autopush.log"

# GPT/Copilot intelligence sources
COPILOT_MEMORY_DIR="$HOME/.vscode-server/data/User/workspaceStorage/ea927e716745397d3dd55dd29b2a87e4/GitHub.copilot-chat/memory-tool/memories/ZDk1NDlmMzItMWI3ZC00ZTA4LTk1MTUtMGFlMDA1ODY3MTVh"
COPILOT_PLAN="$COPILOT_MEMORY_DIR/plan.md"
COPILOT_FLOW="$COPILOT_MEMORY_DIR/pokefirered_intro_control_flow.md"
PROMPT_LOG="/home/spark-advantage/PROMPT_RESPONSE_LOG.md"

# Track what we've already seen so we only report deltas
PLAN_HASH_FILE="/tmp/pfr_autopush_plan_hash"
FLOW_HASH_FILE="/tmp/pfr_autopush_flow_hash"
PROMPTLOG_LINES_FILE="/tmp/pfr_autopush_promptlog_lines"

# Ensure only one instance
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    echo "Already running (PID $(cat "$PIDFILE")). Kill it first or remove $PIDFILE."
    exit 1
fi
echo $$ > "$PIDFILE"
trap 'rm -f "$PIDFILE"; exit' EXIT INT TERM

log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOGFILE"; }

# Initialize hash trackers from current state
hash_file() { md5sum "$1" 2>/dev/null | cut -d' ' -f1 || echo "none"; }

[ -f "$COPILOT_PLAN" ] && hash_file "$COPILOT_PLAN" > "$PLAN_HASH_FILE"
[ -f "$COPILOT_FLOW" ] && hash_file "$COPILOT_FLOW" > "$FLOW_HASH_FILE"
[ -f "$PROMPT_LOG" ] && wc -l < "$PROMPT_LOG" > "$PROMPTLOG_LINES_FILE" || echo 0 > "$PROMPTLOG_LINES_FILE"

log "autopush started (PID $$, interval ${INTERVAL}s)"
log "monitoring copilot memory at: $COPILOT_MEMORY_DIR"
log "monitoring prompt log at: $PROMPT_LOG"

# Extract the latest GPT activity context for commit messages
get_gpt_context() {
    local context=""

    # 1. Check if Copilot's plan.md changed (means GPT updated its strategy)
    if [ -f "$COPILOT_PLAN" ]; then
        local current_hash
        current_hash=$(hash_file "$COPILOT_PLAN")
        local old_hash
        old_hash=$(cat "$PLAN_HASH_FILE" 2>/dev/null || echo "none")
        if [ "$current_hash" != "$old_hash" ]; then
            # Extract the first meaningful line after "## Plan:"
            local plan_summary
            plan_summary=$(head -5 "$COPILOT_PLAN" | grep -v '^$' | grep -v '^#' | head -1 | cut -c1-120)
            if [ -n "$plan_summary" ]; then
                context="[plan updated] $plan_summary"
            fi
            echo "$current_hash" > "$PLAN_HASH_FILE"
        fi
    fi

    # 2. Check if the control flow analysis changed
    if [ -f "$COPILOT_FLOW" ]; then
        local current_hash
        current_hash=$(hash_file "$COPILOT_FLOW")
        local old_hash
        old_hash=$(cat "$FLOW_HASH_FILE" 2>/dev/null || echo "none")
        if [ "$current_hash" != "$old_hash" ]; then
            # Get the last section heading added
            local flow_update
            flow_update=$(grep '^##' "$COPILOT_FLOW" | tail -1 | cut -c1-100)
            if [ -n "$flow_update" ]; then
                context="${context:+$context; }[analysis] $flow_update"
            fi
            echo "$current_hash" > "$FLOW_HASH_FILE"
        fi
    fi

    # 3. Check for new prompt/response log entries (GPT's own session log)
    if [ -f "$PROMPT_LOG" ]; then
        local current_lines
        current_lines=$(wc -l < "$PROMPT_LOG")
        local old_lines
        old_lines=$(cat "$PROMPTLOG_LINES_FILE" 2>/dev/null || echo 0)
        if [ "$current_lines" -gt "$old_lines" ]; then
            # Extract the latest "GPT version:" line which contains the task focus
            local latest_task
            latest_task=$(tail -n +"$old_lines" "$PROMPT_LOG" | grep '^GPT version:' | tail -1 | sed 's/^GPT version: //')
            if [ -n "$latest_task" ]; then
                context="${context:+$context; }[task] $latest_task"
            fi
            echo "$current_lines" > "$PROMPTLOG_LINES_FILE"
        fi
    fi

    # 4. Scan for any new memory files we haven't seen
    if [ -d "$COPILOT_MEMORY_DIR" ]; then
        local new_files
        new_files=$(find "$COPILOT_MEMORY_DIR" -name '*.md' -newer "$PIDFILE" 2>/dev/null | head -3)
        if [ -n "$new_files" ]; then
            local new_names
            new_names=$(echo "$new_files" | xargs -I{} basename {} .md | tr '\n' ', ' | sed 's/,$//')
            context="${context:+$context; }[new notes] $new_names"
        fi
    fi

    echo "$context"
}

# Categorize changed files for a smarter commit subject line
categorize_changes() {
    local files
    files=$(git diff --cached --name-only)

    local has_stubs=false has_smoke=false has_docs=false has_build=false has_host=false
    while IFS= read -r f; do
        case "$f" in
            *stubs*) has_stubs=true ;;
            *smoke*) has_smoke=true ;;
            *.md)    has_docs=true ;;
            CMake*)  has_build=true ;;
            src/host_*) has_host=true ;;
        esac
    done <<< "$files"

    if $has_stubs && $has_smoke; then echo "extend upstream provider + smoke proof"
    elif $has_stubs; then echo "update host stubs"
    elif $has_smoke; then echo "extend smoke tests"
    elif $has_host; then echo "update host layer"
    elif $has_docs; then echo "update docs"
    elif $has_build; then echo "update build config"
    else echo "sync changes"
    fi
}

while true; do
    cd "$REPO"

    # Check for any changes (staged, unstaged, or untracked)
    if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
        # Stage everything except build artifacts (gitignore handles this)
        git add -A

        # Gather intelligence
        CATEGORY=$(categorize_changes)
        GPT_CONTEXT=$(get_gpt_context)
        CHANGED=$(git diff --cached --stat | tail -1)
        TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

        # Build an intelligent commit message
        SUBJECT="auto: ${CATEGORY} (${TIMESTAMP})"
        BODY="${CHANGED}"
        if [ -n "$GPT_CONTEXT" ]; then
            BODY="${BODY}

GPT activity: ${GPT_CONTEXT}"
        fi

        git commit -m "$(cat <<EOF
${SUBJECT}

${BODY}
EOF
)" >> "$LOGFILE" 2>&1

        if git push >> "$LOGFILE" 2>&1; then
            log "pushed: ${CATEGORY} | ${GPT_CONTEXT:-no new GPT context}"
        else
            log "push failed — will retry next cycle"
        fi
    fi

    sleep "$INTERVAL"
done
