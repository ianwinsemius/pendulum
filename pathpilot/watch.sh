#!/bin/bash
# Background helper started at login: whenever PathPilot is running and
# pendulum is not loaded, load it. Covers PathPilot restarts.
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
exec 9>"/tmp/pendulum-watch.lock"
flock -n 9 || exit 0   # only one watcher

while true; do
  "$DIR/attach.sh" >>"$DIR/attach.log" 2>&1
  sleep 3
done
