#!/bin/bash
# Load the ppbox component and its wiring into a running PathPilot.
# Safe to run any number of times. Does nothing if PathPilot isn't running
# or ppbox is already loaded.
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
PY="$(cat "$DIR/python.txt" 2>/dev/null || command -v python2 || command -v python)"

halcmd show comp >/dev/null 2>&1 || exit 0            # LinuxCNC not running
halcmd show comp 2>/dev/null | grep -qw ppbox && exit 0  # already loaded

echo "ppbox: loading component"
halcmd loadusr -Wn ppbox "$PY" "$DIR/ppbox.py" || { echo "ppbox: component failed to load"; exit 1; }
if ! halcmd -f "$DIR/ppbox.hal"; then
  echo "ppbox: wiring failed, unloading"
  halcmd unload ppbox
  exit 1
fi
echo "ppbox: ready"
