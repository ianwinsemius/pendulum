# Source this (". ppenv.sh") to get PathPilot's LinuxCNC environment:
# halcmd on PATH and Python able to "import hal".
#
# PathPilot runs LinuxCNC "run-in-place" from ~/tmc, so a plain terminal has
# neither. In order of preference:
#   running: copy the variables from the running milltask process
#            (same user, so /proc/<pid>/environ is readable)
#   static:  build them from the ~/tmc layout (works when PathPilot is closed)
#   rip:     LinuxCNC's own rip-environment script, if this install has one
# Sets PENDULUM_ENV to running / static / rip / none, and PENDULUM_ENV_WHY
# to a one-line explanation.

PENDULUM_ENV=none
PENDULUM_ENV_WHY=""

# --- 1. copy from the running milltask ------------------------------------
_pp_pid="$(pgrep -u "$(id -u)" -x milltask 2>/dev/null | head -n1)"
[ -n "$_pp_pid" ] || _pp_pid="$(pgrep -u "$(id -un)" milltask 2>/dev/null | head -n1)"
[ -n "$_pp_pid" ] || _pp_pid="$(pgrep milltask 2>/dev/null | head -n1)"
if [ -z "$_pp_pid" ]; then
  PENDULUM_ENV_WHY="milltask not running"
elif [ ! -r "/proc/$_pp_pid/environ" ]; then
  PENDULUM_ENV_WHY="milltask pid $_pp_pid environment not readable"
else
  while IFS= read -r -d '' _pp_kv; do
    case "$_pp_kv" in
      PATH=*|PYTHONPATH=*|LD_LIBRARY_PATH=*|LINUXCNC*|EMC2*|HAL_*|RTAPI*|INI_FILE_NAME=*|TCLLIBPATH=*)
        export "$_pp_kv" ;;
    esac
  done 2>/dev/null < "/proc/$_pp_pid/environ"
  if command -v halcmd >/dev/null 2>&1; then
    PENDULUM_ENV=running
    PENDULUM_ENV_WHY="copied from milltask pid $_pp_pid"
  else
    PENDULUM_ENV_WHY="milltask pid $_pp_pid environment has no halcmd"
  fi
fi

# --- 2. build it from the ~/tmc layout ------------------------------------
if [ "$PENDULUM_ENV" = none ]; then
  _pp_tmc="$(readlink -f "$HOME/tmc" 2>/dev/null)"
  if [ -n "$_pp_tmc" ] && [ -x "$_pp_tmc/bin/halcmd" ]; then
    _pp_ver="$(basename "$_pp_tmc")"            # e.g. v2.14.3
    for _pp_d in "$HOME/$_pp_ver/venv/bin"; do
      [ -d "$_pp_d" ] && PATH="$_pp_d:$PATH"
    done
    export PATH="$PATH:$HOME/tmc/python:$HOME/tmc/bin:$HOME/tmc/scripts"
    export LD_LIBRARY_PATH="$HOME/tmc/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PYTHONPATH="$HOME/tmc/python:$HOME/tmc/lib/python:$HOME/tmc/lib/python/support${PYTHONPATH:+:$PYTHONPATH}"
    export LINUXCNC_HOME="$HOME/tmc" EMC2_HOME="$HOME/tmc"
    export LINUXCNC_BIN_DIR="$HOME/tmc/bin" EMC2_BIN_DIR="$HOME/tmc/bin"
    export LINUXCNC_RTLIB_DIR="$HOME/tmc/rtlib" EMC2_RTLIB_DIR="$HOME/tmc/rtlib"
    PENDULUM_ENV=static
    PENDULUM_ENV_WHY="${PENDULUM_ENV_WHY}; built from $HOME/tmc ($_pp_ver)"
  fi
fi

# --- 3. LinuxCNC's own run-in-place script --------------------------------
if [ "$PENDULUM_ENV" = none ] && [ -f "$HOME/tmc/scripts/rip-environment" ]; then
  # shellcheck disable=SC1091
  . "$HOME/tmc/scripts/rip-environment" >/dev/null 2>&1
  if command -v halcmd >/dev/null 2>&1; then
    PENDULUM_ENV=rip
    PENDULUM_ENV_WHY="${PENDULUM_ENV_WHY}; rip-environment"
  fi
fi

[ "$PENDULUM_ENV" = none ] && PENDULUM_ENV_WHY="${PENDULUM_ENV_WHY}; no ~/tmc/bin/halcmd found"

unset _pp_pid _pp_kv _pp_tmc _pp_ver _pp_d
export PENDULUM_ENV PENDULUM_ENV_WHY
