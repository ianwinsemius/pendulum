#!/bin/bash
# ppbox installer. Run from the ppbox drive:   bash /media/<you>/PPBOX/install.sh
# Safe to run again (after a PathPilot update, or to upgrade).
#
# What it does:
#   1. copies ppbox files to ~/ppbox (replacing any old copy)
#   2. installs a udev rule so PathPilot can read the box  (asks for sudo)
#   3. adds a login item that loads ppbox whenever PathPilot runs
#   4. runs probe.sh and saves ~/ppbox/probe.txt
# It does not edit any PathPilot file. uninst.sh reverses all of it.
set -u
SRC="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
DEST="$HOME/ppbox"
RULE=/etc/udev/rules.d/99-ppbox.rules
AUTOSTART="$HOME/.config/autostart/ppbox-watch.desktop"

say() { echo "ppbox: $*"; }

# 1. files
say "installing to $DEST"
mkdir -p "$DEST"
for f in ppbox.py ppbox.hal attach.sh watch.sh probe.sh uninst.sh readme.txt; do
  if [ -f "$SRC/$f" ]; then
    if [ "$f" = ppbox.hal ] && [ -f "$DEST/ppbox.hal" ] && ! cmp -s "$SRC/$f" "$DEST/$f"; then
      cp "$DEST/ppbox.hal" "$DEST/ppbox.hal.bak"   # keep local tuning
      say "kept your previous ppbox.hal as ppbox.hal.bak"
    fi
    cp "$SRC/$f" "$DEST/$f"
  fi
done
chmod +x "$DEST"/*.sh "$DEST/ppbox.py"

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
if [ -f "$RULE" ] && cmp -s "$SRC/ppbox.rul" "$RULE"; then
  say "udev rule already installed"
else
  say "installing udev rule (needs sudo)"
  if sudo cp "$SRC/ppbox.rul" "$RULE" && sudo udevadm control --reload-rules && sudo udevadm trigger; then
    say "udev rule installed"
  else
    say "WARNING: udev rule NOT installed; ppbox will not be able to read the box"
  fi
fi

# 3. login item
mkdir -p "$(dirname "$AUTOSTART")"
cat >"$AUTOSTART" <<EOF
[Desktop Entry]
Type=Application
Name=ppbox watcher
Exec=$DEST/watch.sh
X-GNOME-Autostart-enabled=true
NoDisplay=true
EOF
say "login item added (takes effect next login; starting it now too)"
nohup "$DEST/watch.sh" >/dev/null 2>&1 &

# 4. probe
"$DEST/probe.sh" "$DEST/probe.txt"
say "done. Unplug and replug the box (it will come back in run mode)."
say "Send $DEST/probe.txt so ppbox.hal can be finalized."
