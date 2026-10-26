#!/bin/bash
# pendulum installer. Run from the pendulum drive:   bash /media/<you>/PENDULUM/install.sh
# Safe to run again (after a PathPilot update, or to upgrade).
#
# What it does:
#   1. copies pendulum files to ~/pendulum (replacing any old copy)
#   2. installs a udev rule so PathPilot can read Pendulum  (asks for sudo)
#   3. adds a login item that loads pendulum whenever PathPilot runs
#   4. runs probe.sh and saves ~/pendulum/probe.txt
# It does not edit any PathPilot file. uninst.sh reverses all of it.
set -u
SRC="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
DEST="$HOME/pendulum"
RULE=/etc/udev/rules.d/99-pendulum.rules
AUTOSTART="$HOME/.config/autostart/pendulum-watch.desktop"

say() { echo "pendulum: $*"; }

# 1. files
say "installing to $DEST"
mkdir -p "$DEST"
for f in pendulum.py pendulum.hal ppenv.sh ppwire.sh attach.sh watch.sh probe.sh uninst.sh readme.txt; do
  if [ -f "$SRC/$f" ]; then
    if [ "$f" = pendulum.hal ] && [ -f "$DEST/pendulum.hal" ] && ! cmp -s "$SRC/$f" "$DEST/$f"; then
      cp "$DEST/pendulum.hal" "$DEST/pendulum.hal.bak"   # keep local tuning
      say "kept your previous pendulum.hal as pendulum.hal.bak"
    fi
    cp "$SRC/$f" "$DEST/$f"
  fi
done
chmod +x "$DEST"/*.sh "$DEST/pendulum.py"

# Pick the Python that can import LinuxCNC's hal module, using PathPilot's
# environment (LinuxCNC runs in place from ~/tmc, so a plain shell can't).
# shellcheck disable=SC1091
. "$DEST/ppenv.sh"
say "PathPilot environment: $PENDULUM_ENV ($PENDULUM_ENV_WHY)"
PY=""
for p in python python2 python2.7 python3; do  # "python" = PathPilot's own (venv) first
  if command -v "$p" >/dev/null && "$p" -c "import hal" >/dev/null 2>&1; then PY="$(command -v "$p")"; break; fi
done
if [ -n "$PY" ]; then
  echo "$PY" >"$DEST/python.txt"
  say "using $PY"
else
  say "WARNING: no Python here can import 'hal' (environment: $PENDULUM_ENV: $PENDULUM_ENV_WHY)."
  say "         Send ~/pendulum/probe.txt; it shows the details."
fi

# If an older pendulum is loaded in the running PathPilot, take it out so the
# watcher reloads it with the new files (jog enables forced off first, the
# ShuttleXpress step button handed back to PathPilot).
if [ "$PENDULUM_ENV" != none ] && halcmd show comp 2>/dev/null | grep -qw pendulum; then
  # shellcheck disable=SC1091
  . "$DEST/ppwire.sh"
  pendulum_unwire
  halcmd unload pendulum && say "unloaded the old component; it reloads with the new wiring within a few seconds"
fi

# 2. udev rule
if [ -f "$RULE" ] && cmp -s "$SRC/pendulum.rul" "$RULE"; then
  say "udev rule already installed"
else
  say "installing udev rule (needs sudo)"
  if sudo cp "$SRC/pendulum.rul" "$RULE" && sudo udevadm control --reload-rules && sudo udevadm trigger; then
    say "udev rule installed"
  else
    say "WARNING: udev rule NOT installed; pendulum will not be able to read Pendulum"
  fi
fi

# 3. login item
mkdir -p "$(dirname "$AUTOSTART")"
cat >"$AUTOSTART" <<EOF
[Desktop Entry]
Type=Application
Name=pendulum watcher
Exec=$DEST/watch.sh
X-GNOME-Autostart-enabled=true
NoDisplay=true
EOF
say "login item added (takes effect next login; starting it now too)"
nohup "$DEST/watch.sh" >/dev/null 2>&1 &

# 4. probe
"$DEST/probe.sh" "$DEST/probe.txt"
say "done. Unplug and replug Pendulum (it will come back in run mode)."
say "Send $DEST/probe.txt so pendulum.hal can be finalized."
