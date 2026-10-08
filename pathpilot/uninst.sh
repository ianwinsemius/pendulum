#!/bin/bash
# Remove everything install.sh added. PathPilot itself was never modified.
set -u
DEST="$HOME/ppbox"
say() { echo "ppbox: $*"; }

pkill -f "$DEST/watch.sh" 2>/dev/null && say "stopped watcher"
if halcmd show comp 2>/dev/null | grep -qw ppbox; then
  halcmd unload ppbox && say "unloaded component"
fi
rm -f "$HOME/.config/autostart/ppbox-watch.desktop" && say "removed login item"
if [ -f /etc/udev/rules.d/99-ppbox.rules ]; then
  sudo rm -f /etc/udev/rules.d/99-ppbox.rules && sudo udevadm control --reload-rules && say "removed udev rule"
fi
if [ -d "$DEST" ]; then
  mv "$DEST" "$DEST.removed.$(date +%Y%m%d%H%M%S)" && say "moved $DEST aside (delete it when you're sure)"
fi
say "uninstalled"
