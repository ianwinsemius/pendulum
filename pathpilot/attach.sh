#!/bin/bash
# Load the pendulum component and its wiring into a running PathPilot.
# Safe to run any number of times. Does nothing if PathPilot isn't running
# or pendulum is already loaded.
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
PY="$(cat "$DIR/python.txt" 2>/dev/null || command -v python2 || command -v python)"

halcmd show comp >/dev/null 2>&1 || exit 0            # LinuxCNC not running
halcmd show comp 2>/dev/null | grep -qw pendulum && exit 0  # already loaded

echo "pendulum: loading component"
halcmd loadusr -Wn pendulum "$PY" "$DIR/pendulum.py" || { echo "pendulum: component failed to load"; exit 1; }
if ! halcmd -f "$DIR/pendulum.hal"; then
  echo "pendulum: wiring failed, unloading"
  halcmd unload pendulum
  exit 1
fi
echo "pendulum: ready"
