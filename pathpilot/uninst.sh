#!/bin/bash
# Remove everything install.sh added. PathPilot itself was never modified.
set -u
DEST="$HOME/pendulum"
say() { echo "pendulum: $*"; }
# shellcheck disable=SC1091
[ -f "$DEST/ppenv.sh" ] && . "$DEST/ppenv.sh"

pkill -f "$DEST/watch.sh" 2>/dev/null && say "stopped watcher"
if halcmd show comp 2>/dev/null | grep -qw pendulum; then
  # Disable hand-wheel jogging, hand the step button back, then unload.
  # shellcheck disable=SC1091
  . "$DEST/ppwire.sh"
  pendulum_unwire
  halcmd unload pendulum && say "unloaded component"
fi
rm -f "$HOME/.config/autostart/pendulum-watch.desktop" && say "removed login item"
if [ -f /etc/udev/rules.d/99-pendulum.rules ]; then
  sudo rm -f /etc/udev/rules.d/99-pendulum.rules && sudo udevadm control --reload-rules && say "removed udev rule"
fi
if [ -d "$DEST" ]; then
  mv "$DEST" "$DEST.removed.$(date +%Y%m%d%H%M%S)" && say "moved $DEST aside (delete it when you're sure)"
fi
say "uninstalled"
