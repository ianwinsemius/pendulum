#!/bin/bash
# Collect what's needed to finalize pendulum.hal for this controller.
# Read-only: changes nothing. Run with PathPilot open (machine can be off).
# Output: probe.txt in the current folder (or the path given as $1).
DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
OUT="${1:-$PWD/probe.txt}"
# shellcheck disable=SC1091
. "$DIR/ppenv.sh"

{
  echo "== pendulum probe 2 $(date)"
  echo "== system"; uname -a; cat /etc/lsb-release 2>/dev/null
  echo "== pathpilot"; ls -ld ~/v* ~/tmc 2>/dev/null; cat ~/tmc/version.json 2>/dev/null
  ps -eo args | grep -E "linuxcnc|milltask|tormach|pathpilot|halui" | grep -v grep | head -20
  echo "== environment (source: $PENDULUM_ENV: $PENDULUM_ENV_WHY)"
  echo "PATH=$PATH"; echo "PYTHONPATH=$PYTHONPATH"; echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
  env | grep -E "^(LINUXCNC|EMC2|HAL_|RTAPI|INI_FILE_NAME)" | sort
  echo "== milltask processes"; ps -o user,pid,ppid,comm,args -C milltask 2>&1
  echo "== halcmd"; command -v halcmd || echo "halcmd not found"
  if halcmd show comp >/dev/null 2>&1; then echo "HAL reachable"; else echo "HAL not reachable"; fi
  echo "== python"
  for p in python python2 python2.7 python3; do
    if command -v $p >/dev/null; then
      printf "%s (%s): " "$p" "$(command -v $p)"
      $p -c "import sys,hal; print(sys.version.split()[0] + ' hal OK ' + hal.__file__)" 2>&1 | tail -1
    fi
  done
  echo "== components"; halcmd show comp 2>&1
  echo "== jog pins (axis/joint)"; halcmd show pin 2>&1 | grep -E "(axis|joint)\.[0-9a-z]+\.jog-"
  echo "== jog params"; halcmd show param 2>&1 | grep -iE "jog"
  echo "== halui jog pins"; halcmd show pin halui 2>&1 | grep -iE "jog|mode|machine\.is-on|estop"
  echo "== tormach / shuttle pins"; halcmd show pin 2>&1 | grep -iE "tormach\.|sxp|shuttle|pendulum"
  echo "== jog signals"; halcmd show sig 2>&1 | grep -iE "jog|sxp|shuttle"
  echo "== motion state pins"; halcmd show pin 2>&1 | grep -E "motion\.(motion-enabled|in-position|teleop-mode|coord-mode)|axis\.[0-9a-z]+\.(pos-cmd|kb-jog-active|wheel-jog-active)"
  INI="$(ps -eo args | grep -oE '/[^ ]+\.ini' | sort -u | head -n1)"
  echo "== ini: $INI"
  if [ -f "$INI" ]; then
    grep -nE "^\[|HALFILE|HALUI|JOG|MAX_LINEAR|MAX_VELOCITY|MAX_ACCEL|LINEAR_UNITS|INCREMENTS|MIN_LIMIT|MAX_LIMIT|COORDINATES|KINEMATICS" "$INI"
    CFG="$(dirname "$INI")"
    for f in millshuttlexpress.hal postgui_tormach_mill.hal; do
      echo "== file: $CFG/$f"
      cat "$CFG/$f" 2>&1
    done
    echo "== jog lines in other hal files"
    grep -nHiE "jog|halui" "$CFG"/*.hal "$CFG"/../common/*.hal 2>/dev/null | grep -v "millshuttlexpress.hal\|postgui_tormach_mill.hal" | head -80
  fi
  echo "== shuttle driver"; command -v shuttlexpress shuttle 2>&1; ls ~/tmc/bin 2>/dev/null | grep -iE "shuttle|hal_input|hidcomp"
  echo "== usb"; lsusb 2>&1
  echo "== hidraw"
  for d in /sys/class/hidraw/hidraw*; do
    [ -e "$d" ] || continue
    echo "$(basename "$d"): $(grep HID_ID "$d/device/uevent" 2>/dev/null) $(grep HID_NAME "$d/device/uevent" 2>/dev/null)"
    ls -l "/dev/$(basename "$d")"
  done
  echo "== pendulum install"; ls -l ~/pendulum 2>&1; cat ~/pendulum/python.txt 2>/dev/null; tail -20 ~/pendulum/attach.log 2>/dev/null
  echo "== udev"; cat /etc/udev/rules.d/99-pendulum.rules 2>&1
} >"$OUT" 2>&1
echo "pendulum: wrote $OUT (environment: $PENDULUM_ENV: $PENDULUM_ENV_WHY)"
# Judge by whether HAL actually answers, not by how the environment was found.
if halcmd show comp >/dev/null 2>&1; then
  echo "pendulum: HAL is reachable; probe.txt has the HAL sections."
else
  echo "pendulum: HAL did not answer (is PathPilot open?). Start PathPilot and run ~/pendulum/probe.sh again for the HAL sections."
fi
true
