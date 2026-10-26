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
# PathPilot mill inch increments by sxp.gui_step_index (checked on the PCNC
# 1100, 2026-10-25: .0001=0 .001=1 .01=2 .1=3).
INCH_STEPS = [0.0001, 0.001, 0.01, 0.1]
STEP_PULSE_S = 0.25     # how long a Y press holds PathPilot's step button
STEP_LOCKOUT_S = 0.35   # ignore further Y presses for this long
LED_RESEND_S = 2.0
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
c.newpin("step-size", hal.HAL_FLOAT, hal.HAL_IN)       # used when step-follow is off

# Follow PathPilot's jog increment (sxp.gui_step_index / sxp.is_metric).
c.newpin("step-follow", hal.HAL_BIT, hal.HAL_IN)       # setp 1 in pendulum.hal
c.newpin("gui-step-index", hal.HAL_U32, hal.HAL_IN)
c.newpin("is-metric", hal.HAL_BIT, hal.HAL_IN)
c.newpin("step-active", hal.HAL_FLOAT, hal.HAL_OUT)    # step in use now (0 = steps off)
# Y on the pendant presses PathPilot's jog-step button. The ShuttleXpress
# step button is relayed through here too (attach.sh rewires it).
c.newpin("shuttle-step-in", hal.HAL_BIT, hal.HAL_IN)
c.newpin("step-button-out", hal.HAL_BIT, hal.HAL_OUT)

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
        self.y_was = False
        self.pulse_until = 0.0
        self.lockout_until = 0.0
        self.led_sent = None
        self.led_time = 0.0

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
    f = REPORT.unpack(bytes(data[:REPORT.size]))  # bytes() = str on Python 2.7
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
                step = current_step()
                if scale > 0 and step > 0:
                    st.jog_acc[i] += d * step / scale
    st.last_raw_counts = list(counts)
    return True


def current_step():
    """Step distance in machine units (inch), or 0 to disable steps."""
    if not c["step-follow"]:
        return c["step-size"]
    if c["is-metric"]:
        return 0.0  # metric increments not mapped yet: steps off rather than wrong
    i = c["gui-step-index"]
    return INCH_STEPS[i] if 0 <= i < len(INCH_STEPS) else 0.0


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


def step_button(st, now):
    """Relay the shuttle's step button and add a pulse for pendant Y."""
    y = bool(c["btn-y"]) and c["connected"]
    if y and not st.y_was and now >= st.lockout_until:
        st.pulse_until = now + STEP_PULSE_S
        st.lockout_until = now + STEP_LOCKOUT_S
    st.y_was = y
    c["step-button-out"] = bool(c["shuttle-step-in"]) or now < st.pulse_until


def send_led(st, fd, now):
    """Tell Pendulum which player LED to light: step index 0..3 -> LED 1..4."""
    if fd is None:
        st.led_sent = None
        return
    i = c["gui-step-index"]
    led = (i + 1) if (c["step-follow"] and not c["is-metric"] and 0 <= i < 4) else 0
    if led == st.led_sent and now - st.led_time < LED_RESEND_S:
        return
    try:
        # hidraw: first byte is the report number (0 = no report IDs)
        os.write(fd, bytes(bytearray([0, PROTOCOL, led, 0, 0, 0, 0, 0, 0])))
        st.led_sent, st.led_time = led, now
    except OSError:
        st.led_time = now  # try again later; reads will notice a real unplug


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
                        try:
                            fd = os.open(dev, os.O_RDWR | os.O_NONBLOCK)
                        except OSError:
                            fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)  # no LEDs
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
                            st.led_sent = None
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
            c["step-active"] = current_step()
            step_button(st, now)
            send_led(st, fd, now)
            last = now
    except KeyboardInterrupt:
        pass
    finally:
        zero_outputs(st)


if __name__ == "__main__":
    main()
