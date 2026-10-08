ppbox - gamepad jog adapter for PathPilot
==========================================

On the PathPilot controller, open a terminal and run:

    bash /media/$USER/PPBOX/install.sh

(The drive may be mounted somewhere else; `ls /media/$USER` shows it.)

Then unplug and replug the box. It comes back in run mode.

After a PathPilot update: hold Back+Start on the pendant for 3 seconds to
get this drive back, and run install.sh again.

Files
  install.sh   install or reinstall (safe to repeat)
  uninst.sh    remove everything install.sh added
  probe.sh     read-only report of this controller's HAL setup
  ppbox.py     the HAL component that reads the box
  ppbox.hal    wiring and tuning
  attach.sh    loads ppbox into a running PathPilot
  watch.sh     keeps ppbox loaded across PathPilot restarts
  ppbox.rul    udev rule (installed as 99-ppbox.rules)

Pendant combos (hold Back + ...)
  Start, 3 s          switch run / config (this drive) mode
  Start+LB+RB, 3 s    firmware update mode (RPI-RP2 drive)

Nothing in PathPilot's own configuration is modified.
