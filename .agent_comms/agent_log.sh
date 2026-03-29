#!/bin/bash
# Usage: agent_log.sh <agent_id> <status> <message>
LOGFILE=/home/spark-advantage/pokefirered-native/.agent_comms/agent_log.tsv
AGENT_ID="$1"
STATUS="$2"
MSG="$3"
TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%S.%3NZ)
(
  flock -w 5 200
  echo -e "${TIMESTAMP}\t${AGENT_ID}\t${STATUS}\t${MSG}" >> "$LOGFILE"
) 200>/home/spark-advantage/pokefirered-native/.agent_comms/.lock
