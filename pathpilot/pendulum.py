#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""pendulum HAL component: reads the pendulum USB adapter and exposes HAL pins.

Loaded at runtime by attach.sh; nothing in PathPilot's own config is edited.
Works with Python 2.7 (PathPilot 2.x) and Python 3.

Safety rules (enforced here, independent of Pendulum):
  * velocities are 0 unless reports are arriving, current, and the right
    protocol version;
  * a missing report for STALE_S seconds, a read error, or an unplug drops
    everything to 0 and stops generating jog counts;
  * Pendulum being unplugged does not unload this component, so HAL nets stay
    intact and it reconnects by itself.
"""
from __future__ import division, print_function

import glob
import os
import select
import struct
import sys
import time

import hal

VID, PID = 0x1209, 0x0001
PROTOCOL = 1
REPORT = struct.Struct("<BBBB4hH4B2x")
STALE_S = 0.100
RESCAN_S = 1.0
AXES = "xyza"
BUTTONS = [  # XInput bit layout, as sent by Pendulum
    ("dpad-up", 0x0001), ("dpad-down", 0x0002), ("dpad-left", 0x0004),
    ("dpad-right", 0x0008), ("start", 0x0010), ("l3", 0x0040), ("r3", 0x0080),
    ("lb", 0x0100), ("rb", 0x0200), ("guide", 0x0400), ("a", 0x1000),
    ("b", 0x2000), ("x", 0x4000), ("y", 0x8000),
]

c = hal.component("pendulum")
c.newpin("connected", hal.HAL_BIT, hal.HAL_OUT)    # Pendulum present and healthy
c.newpin("pendant", hal.HAL_BIT, hal.HAL_OUT)      # gamepad plugged into Pendulum
c.newpin("armed", hal.HAL_BIT, hal.HAL_OUT)
c.newpin("motion", hal.HAL_BIT, hal.HAL_OUT)
c.newpin("fault", hal.HAL_BIT, hal.HAL_OUT)        # version mismatch / stale
c.newpin("owner", hal.HAL_S32, hal.HAL_OUT)        # -1 none, 0 XY(planar)/X, 2 Z ...

# Machine-units-per-count for jog-counts outputs, and full-stick speed.
c.newpin("jog-scale", hal.HAL_FLOAT, hal.HAL_IN)        # e.g. 0.0001 in/count
c.newpin("max-speed", hal.HAL_FLOAT, hal.HAL_IN)        # units/s at full stick
# Single-step distance for D-pad presses (units). Wire to PathPilot's step
# size if available, otherwise set it in pendulum.hal.
c.newpin("step-size", hal.HAL_FLOAT, hal.HAL_IN)

for a in AXES:
    c.newpin(a + "-vel", hal.HAL_FLOAT, hal.HAL_OUT)       # -1..+1
    c.newpin(a + "-steps", hal.HAL_S32, hal.HAL_OUT)       # raw D-pad step count
    c.newpin(a + "-jog-counts", hal.HAL_S32, hal.HAL_OUT)  # stick + steps, in jog-scale units
for name, _ in BUTTONS:
    c.newpin("btn-" + name, hal.HAL_BIT, hal.HAL_OUT)
c.ready()


def log(msg):
    sys.stderr.write("pendulum: %s\n" % msg)
    sys.stderr.flush()


def find_device():
    want = "HID_ID=0003:%08X:%08X" % (VID, PID)
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            with open(os.path.join(d, "device", "uevent")) as f:
                if want in f.read().upper():
                    return "/dev/" + os.path.basename(d)
        except (IOError, OSError):
            pass
    return None


class State(object):
    def __init__(self):
        self.reset()
        self.jog_acc = [0.0] * 4       # integrated stick motion, in counts
        self.jog_out = [0] * 4
        self.steps = [0] * 4
        self.last_raw_counts = None

    def reset(self):
        self.vel = [0.0] * 4
        self.last_report = 0.0


def zero_outputs(st):
    st.reset()
    st.last_raw_counts = None
    for a in AXES:
        c[a + "-vel"] = 0.0
    for name, _ in BUTTONS:
        c["btn-" + name] = False
    c["pendant"] = False
    c["armed"] = False
    c["motion"] = False
    c["owner"] = -1


def apply_report(st, data, now):
    if len(data) < REPORT.size:
        return False
    f = REPORT.unpack(data[:REPORT.size])
    version, seq, flags, owner = f[0], f[1], f[2], f[3]
    vel, buttons, counts = f[4:8], f[8], f[9:13]
    if version != PROTOCOL:
        if not c["fault"]:
            log("protocol %d, expected %d: ignoring Pendulum (update pendulum.py or firmware)"
                % (version, PROTOCOL))
        c["fault"] = True
        return False
    c["fault"] = False
    st.last_report = now
    c["pendant"] = bool(flags & 0x01)
    c["armed"] = bool(flags & 0x02)
    c["motion"] = bool(flags & 0x08)
    c["owner"] = -1 if owner == 0xFF else owner
    st.vel = [v / 32767.0 for v in vel]
    for i, a in enumerate(AXES):
        c[a + "-vel"] = st.vel[i]
    for name, bit in BUTTONS:
        c["btn-" + name] = bool(buttons & bit)
    # D-pad steps: Pendulum sends wrapping 8-bit counters; track deltas.
    if st.last_raw_counts is not None:
        for i in range(4):
            d = (counts[i] - st.last_raw_counts[i]) & 0xFF
            if d > 127:
                d -= 256
            st.steps[i] += d
            if d:
                scale = c["jog-scale"]
                if scale > 0:
                    st.jog_acc[i] += d * c["step-size"] / scale
    st.last_raw_counts = list(counts)
    return True


def integrate(st, dt):
    """Turn stick velocity into jog counts (MPG-style jogging)."""
    scale, speed = c["jog-scale"], c["max-speed"]
    for i, a in enumerate(AXES):
        if scale > 0 and speed > 0:
            st.jog_acc[i] += st.vel[i] * speed / scale * dt
        whole = int(st.jog_acc[i])  # truncate toward zero
        st.jog_out[i] = whole
        c[a + "-jog-counts"] = whole
        c[a + "-steps"] = st.steps[i]


def main():
    st = State()
    fd, dev, last_scan = None, None, 0.0
    c["owner"] = -1
    last = time.time()
    try:
        while True:
            now = time.time()
            if fd is None and now - last_scan >= RESCAN_S:
                last_scan = now
                dev = find_device()
                if dev:
                    try:
                        fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
                        log("opened %s" % dev)
                        st.last_report = now
                    except OSError as e:
                        log("cannot open %s: %s (udev rule installed?)" % (dev, e))
                        fd = None
            if fd is not None:
                r, _, _ = select.select([fd], [], [], 0.010)
                if r:
                    try:
                        while True:
                            data = os.read(fd, 64)
                            if not data:
                                raise OSError("EOF")
                            apply_report(st, bytearray(data), time.time())
                    except OSError as e:
                        if getattr(e, "errno", None) not in (11, 35):  # EAGAIN
                            log("lost %s (%s)" % (dev, e))
                            os.close(fd)
                            fd = None
                            zero_outputs(st)
            else:
                time.sleep(0.010)

            now = time.time()
            healthy = fd is not None and not c["fault"] and now - st.last_report < STALE_S
            if not healthy:
                st.vel = [0.0] * 4
                for a in AXES:
                    c[a + "-vel"] = 0.0
                c["motion"] = False
            c["connected"] = healthy
            integrate(st, min(now - last, 0.05))
            last = now
    except KeyboardInterrupt:
        pass
    finally:
        zero_outputs(st)


if __name__ == "__main__":
    main()
