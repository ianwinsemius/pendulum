#!/bin/bash
# Load the pendulum component and its wiring into a running PathPilot.
# Safe to run any number of times. Does nothing if PathPilot isn't running
# or pendulum is already loaded.
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
# shellcheck disable=SC1091
. "$DIR/ppenv.sh"                                       # PathPilot's halcmd + Python paths
# shellcheck disable=SC1091
. "$DIR/ppwire.sh"                                      # pendulum_unwire
[ "$PENDULUM_ENV" = none ] && exit 0                     # no LinuxCNC install found
PY="$(cat "$DIR/python.txt" 2>/dev/null || command -v python2 || command -v python)"
INI="${INI_FILE_NAME:-$(ps -eo args | grep -oE '/[^ ]+\.ini' | head -n1)}"

halcmd show comp >/dev/null 2>&1 || exit 0              # HAL not up yet
halcmd show comp 2>/dev/null | grep -qw pendulum && exit 0  # already loaded

say() { echo "pendulum: $(date '+%F %T') $*"; }

# 1. Clean slate: enables forced off, leftovers removed, shuttle step restored.
pendulum_unwire

# 2. Load the component and the counter / step-size wiring. Counters may jump
#    here (new pins start at 0); with the enables off, motion ignores it.
say "loading component"
halcmd loadusr -Wn pendulum "$PY" "$DIR/pendulum.py" || { say "component failed to load"; exit 1; }
if ! halcmd -i "$INI" -f "$DIR/pendulum.hal"; then
  say "wiring failed, unloading"
  pendulum_unwire
  halcmd unload pendulum
  exit 1
fi

# 3. Step button: PathPilot's input now comes from pendulum, which relays the
#    ShuttleXpress step button and adds pendant Y presses.
halcmd unlinkp tormach.jog-step-button
halcmd net sxp.step-button pendulum.shuttle-step-in
halcmd net pendulum-step-out pendulum.step-button-out '=>' tormach.jog-step-button \
  || { say "step-button wiring failed (Y won't change the step)"; pendulum_unwire; halcmd unload pendulum; exit 1; }

# 4. Let motion register the new counter values, then connect the enables.
sleep 0.5
halcmd net pendulum-ok pendulum.connected '=>' axis.0.jog-enable axis.1.jog-enable axis.2.jog-enable \
  || { say "enable wiring failed"; exit 1; }
say "ready (ini $INI)"
