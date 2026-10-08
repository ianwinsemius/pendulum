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
for f in pendulum.py pendulum.hal attach.sh watch.sh probe.sh uninst.sh readme.txt; do
  if [ -f "$SRC/$f" ]; then
    if [ "$f" = pendulum.hal ] && [ -f "$DEST/pendulum.hal" ] && ! cmp -s "$SRC/$f" "$DEST/$f"; then
      cp "$DEST/pendulum.hal" "$DEST/pendulum.hal.bak"   # keep local tuning
      say "kept your previous pendulum.hal as pendulum.hal.bak"
    fi
    cp "$SRC/$f" "$DEST/$f"
  fi
done
chmod +x "$DEST"/*.sh "$DEST/pendulum.py"

# Pick the Python that can import LinuxCNC's hal module.
PY=""
for p in python2 python2.7 python python3; do
  if command -v "$p" >/dev/null && "$p" -c "import hal" >/dev/null 2>&1; then PY="$(command -v "$p")"; break; fi
done
if [ -n "$PY" ]; then
  echo "$PY" >"$DEST/python.txt"
  say "using $PY"
else
  say "WARNING: no Python here can import 'hal'. Is PathPilot installed? (probe.txt will show)"
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
