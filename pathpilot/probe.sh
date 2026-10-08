#!/bin/bash
# Collect what's needed to finalize pendulum.hal for this controller.
# Read-only: changes nothing. Run with PathPilot open (machine can be off).
# Output: probe.txt in the current folder (or the path given as $1).
OUT="${1:-$PWD/probe.txt}"
{
  echo "== pendulum probe $(date)"
  echo "== system"; uname -a; cat /etc/lsb-release 2>/dev/null
  echo "== pathpilot"; ls -d ~/v* ~/tmc 2>/dev/null; cat ~/tmc/version.json 2>/dev/null
  ps -eo args | grep -E "linuxcnc|milltask|tormach|pathpilot" | grep -v grep | head -20
  echo "== python"
  for p in python python2 python2.7 python3; do
    if command -v $p >/dev/null; then
      printf "%s: " "$p"; $p -c "import sys,hal; print(sys.version.split()[0], 'hal OK')" 2>&1 | tail -1
    fi
  done
  echo "== halcmd"; command -v halcmd || echo "halcmd not on PATH"
  echo "== components"; halcmd show comp 2>&1
  echo "== jog pins (axis/joint)"; halcmd show pin 2>&1 | grep -E "(axis|joint)\.[0-9a-z]+\.jog-"
  echo "== halui pins"; halcmd show pin halui 2>&1 | head -60
  echo "== tormach jog pins"; halcmd show pin 2>&1 | grep -iE "tormach\.jog|sxp|shuttle"
  echo "== jog signals"; halcmd show sig 2>&1 | grep -iE "jog|sxp|shuttle"
  echo "== hal files in use"; halcmd show comp 2>&1 | head -1 >/dev/null
  for f in $(ps -eo args | grep -oE "[^ ]+\.ini" | sort -u); do
    echo "-- $f"; grep -E "HALFILE|POSTGUI|HALUI|\[HAL\]" "$f" 2>/dev/null
  done
  echo "== usb"; lsusb 2>&1
  echo "== hidraw"
  for d in /sys/class/hidraw/hidraw*; do
    [ -e "$d" ] || continue
    echo "$(basename "$d"): $(grep HID_ID "$d/device/uevent" 2>/dev/null) $(grep HID_NAME "$d/device/uevent" 2>/dev/null)"
    ls -l "/dev/$(basename "$d")"
  done
  echo "== autostart"; ls ~/.config/autostart 2>&1; echo "XDG_CURRENT_DESKTOP=$XDG_CURRENT_DESKTOP DESKTOP_SESSION=$DESKTOP_SESSION"
  echo "== sudo"; sudo -n true 2>&1 && echo "passwordless sudo" || echo "sudo needs password (or unavailable)"
} >"$OUT" 2>&1
echo "pendulum: wrote $OUT"
