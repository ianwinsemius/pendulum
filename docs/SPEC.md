# Pendulum — gamepad jog adapter for PathPilot

**Status:** v0.4.0. Verified on hardware (Pico W + 8BitDo SN30 Pro wired): firmware boots, serial console, config-mode installer drive, gamepad input decoded. Not yet tested: jog output tuning, PathPilot side. Machine target: Tormach PCNC 1100, X/Y/Z.

## 1. Goal

Use an ordinary USB gamepad as a jog pendant for PathPilot. The pendant is the only thing the operator touches. Pendulum sits between the pendant and the controller and is a pure, reconfigurable translator:

```
 gamepad ──USB──► [ pendulum: Pico W ] ──USB──► PathPilot controller
                    all mapping, curves,        small HAL component
                    safety rules, profiles      + wiring (no PathPilot
                                                 files edited)
```

Design rules:

1. **All behaviour lives in Pendulum.** The PC side is fixed wiring.
2. **Fixed output contract.** Pendulum always sends the same 20-byte report, whatever pendant is plugged in. Swapping pendants or retuning never touches PathPilot.
3. **Fail to zero.** Any doubt (unplug, stale data, wrong version, mode combo, no neutral seen) means zero velocity.
4. **Survives PathPilot updates.** The installer is carried on Pendulum itself and is safe to re-run.

## 2. Hardware

| Item | Part |
|---|---|
| MCU | Raspberry Pi Pico W (RP2040). Wi-Fi unused. |
| PC side | Pico's own micro-USB port (USB device) |
| Pendant side | USB-A female jack, driven by Pico-PIO-USB on GPIO |
| Pendant | 8BitDo SN30 Pro wired (expected to enumerate as XInput) |

### Pendant jack wiring

| USB-A pin | Signal | Pico W pin |
|---|---|---|
| 1 | VBUS 5 V | pin 40 (VBUS) |
| 2 | D− | pin 2 (GP1) |
| 3 | D+ | pin 1 (GP0) |
| 4 | GND | pin 3 (GND) |

D− must be the pin after D+ (set by `PENDULUM_HOST_DP_PIN` in `pendulum.h`). Optional 22–27 Ω series resistors on D+/D− help signal quality; many builds work without. VBUS is only live when the Pico is powered from the PC port, which is the normal case.

Bootloader fallback: the Pico's BOOTSEL button. Drill a pinhole over it.

## 3. Firmware architecture

| Core | Job |
|---|---|
| Core 1 | USB host on PIO pins: enumerates the pendant, decodes reports into a normalized `pad_state_t` |
| Core 0 | Native USB device, jog logic every 8 ms, report out, serial console |

Supported pendants:

- **XInput** (Xbox-style; most 8BitDo pads in X-input mode): fully decoded via the `tusb_xinput` driver.
- **Generic HID**: sticks via configurable byte offsets (`hid.lx` etc.); buttons not yet decoded. Use `log on` to find offsets.

### USB modes (chosen at boot)

| Mode | PID | Interfaces | Purpose |
|---|---|---|---|
| Run | 1209:0001 | HID (jog report) + CDC console | Normal use |
| Config | 1209:0002 | Read-only USB drive + CDC console | Carries the installer |

A power cycle always returns to run mode. 0x1209 is pid.codes; 0001/0002 are its test PIDs — fine for a personal build.

### Pendant combos (hold Select + …)

Select is the 8BitDo label; XInput calls it Back. L/R are the top shoulder buttons (LB/RB).

| Combo | Hold | Action |
|---|---|---|
| Start | 3 s | Toggle run / config mode (reboots Pendulum) |
| Start + LB + RB | 3 s | UF2 bootloader (firmware update) |

While Select is held, all motion is suppressed. After any combo Pendulum requires neutral again before it will move anything.

## 4. Jog behaviour

### Default mapping

| Input | Output |
|---|---|
| Left stick X / Y | X / Y velocity |
| Right stick Y | Z velocity |
| Right stick X | unused (A on a 4th-axis machine) |
| D-pad ←/→ | X single step −/+ |
| D-pad ↓/↑ | Y single step −/+ |
| LB / RB | Z single step −/+ |
| Y | presses PathPilot's jog-step button (cycles .0001/.001/.01/.1); D-pad steps use PathPilot's selected increment; player LEDs 1–4 show it |
| A, B, X, Start, L3, R3, Guide | passed through as button pins (A/B/X actions planned) |

### Arbitration

**free** (default): every stick axis moves independently, so X, Y and Z can jog at the same time. **planar**: X and Y share a group (diagonals), Z alone. **single**: one axis at a time. In planar/single, axes belong to groups:

- The first input past the claim threshold makes its group the *owner*.
- Only the owner group moves. Other inputs are ignored.
- Ownership releases only when **every** stick is below the release threshold and no step button is held.
- A step button pressed while another group owns is *blocked* until released, so it can't fire later by surprise.

### Curves

Each stick: deadzone (`release`) → normalize → expo blend `(1−e)·t + e·t³` → cap (`max`). Claim/release hysteresis stops chatter at the edge of the deadzone.

### Debounce

Starting motion is debounced; stopping never is. A stick must stay deflected (same direction, ending past `claim`) for `start_ms` (default 80 ms) before it drives anything, so finger slips and grazes do nothing. A D-pad/bumper press must be held `step_ms` (default 40 ms) before it steps. Releasing a stick or button stops immediately. The plug-in lockout and ownership use raw deflection, so a stick that is still debouncing counts as deflected.

### Spring-back guard

Released sticks overshoot past centre for a few ms (measured on the 8BitDo: −31744 → +17152 → −4096 → 0). Motion opposite to the last driven direction on a channel is ignored until that stick has rested at centre for `reverse_ms` (default 100 ms; 0 disables). Deliberate reversals just need a brief pause at centre.

### Safety rules

| Condition | Result |
|---|---|
| Pendant not connected / unplugged | All zero |
| Pendant plugged in with a stick deflected or button held | Zero until everything is seen neutral (lockout) |
| Select (Back) held | Zero (combo in progress) |
| Deadman configured but not held | Zero |
| Pendulum unplugged, report stale >100 ms, wrong protocol | PC component zeroes outputs |

Deadman (`set deadman rt` etc.) is off by default. Recommended if the pendant hangs where a stick can be bumped.

## 5. Output report contract (protocol v1)

20 bytes, little-endian, HID input report with no report ID, vendor usage page 0xFF00, sent every 8 ms even when nothing changes.

| Byte | Field | Notes |
|---|---|---|
| 0 | version | = 1. Component ignores anything else. |
| 1 | seq | +1 per report. Heartbeat. |
| 2 | flags | bit0 connected, bit1 armed, bit2 deadman ok, bit3 motion, bit4 suppressed |
| 3 | owner | owning group, 0xFF none |
| 4–11 | vel[4] | int16 X, Y, Z, A. ±32767 = ±100 % of max jog speed |
| 12–13 | buttons | XInput bit layout, Select/Back always masked |
| 14–17 | counts[4] | uint8 X, Y, Z, A single-step counters, wrap at 256 |
| 18–19 | reserved | 0 |

Any change to this layout bumps the version byte, and the component must be updated with it.

### Host → Pendulum report (v1)

8-byte HID output report written by `pendulum.py` to `/dev/hidraw` (report number 0 first): byte 0 version (1), byte 1 LED 0–4 (0 = off, n = player LED n), byte 2 rumble (reserved), bytes 3–7 reserved.

## 6. Configuration

Console diagnostics: `pad` (state + read counters), `log on|raw|off` (pendant changes / every packet), `jog on|off` (the report sent to PathPilot, on change). Hotkeys, no Enter: Ctrl-G jog monitor, Ctrl-L pendant log, Ctrl-P pendant status, Ctrl-C all logging off.

Settings live in Pendulum's flash, edited over the serial console (VS Code Serial Monitor, `screen /dev/ttyACM0`, PuTTY…). `show`, `set <key> <value>`, `save`.

| Key | Default | Meaning |
|---|---|---|
| `claim` | 15 | % deflection that starts a jog |
| `release` | 8 | % below which a stick is neutral |
| `expo` | 40 | 0 linear … 100 cubic |
| `max` | 100 | % cap on output |
| `mode` | free | `free`, `planar` or `single` |
| `deadman` | none | `none`, `lt`, `rt`, `lb`, `rb` |
| `map.lx` … `map.ry` | -x, -y, none, z | target axis; prefix `-` to invert. Default: sticks follow the table (stick right = table right = X−, stick up = table away = Y−, right stick up = spindle up = Z+). D-pad/bumper steps follow the same inversion. |
| `dpad` | 1 | D-pad/bumper single steps on/off |
| `repeat_delay` / `repeat_hz` | 400 / 8 | step auto-repeat |
| `combo_ms` | 3000 | combo hold time |
| `reverse_ms` | 100 | centre dwell before a stick may drive the opposite way |
| `start_ms` | 80 | stick must stay deflected this long before it moves anything |
| `step_ms` | 40 | D-pad/bumper press must be held this long to step |
| `hid.lx` … `hid.ry` | off | byte offsets for generic HID pads |

Planned: editable `config.txt` on the drive instead of the console.

## 7. PathPilot side

Everything installs to `~/pendulum`. No PathPilot file is modified.

| File | Job |
|---|---|
| `pendulum.py` | Userspace HAL component. Finds Pendulum by VID/PID, reads `/dev/hidraw`, publishes pins, enforces the stale/version watchdog. Python 2.7 and 3. |
| `pendulum.hal` | Tuning + wiring (`setp`/`net`), loaded at runtime |
| `attach.sh` | Loads component + wiring into a running PathPilot via `halcmd`; idempotent |
| `watch.sh` | Login item; re-runs attach every 3 s so PathPilot restarts are covered |
| `pendulum.rul` | udev rule → `/etc/udev/rules.d/99-pendulum.rules` (read access, keeps ModemManager off the console) |
| `install.sh` / `uninst.sh` | Install / remove all of the above |
| `probe.sh` | Read-only report of the controller's HAL layout |

### Component pins

| Pin | Dir | Type | Meaning |
|---|---|---|---|
| `pendulum.connected` | out | bit | Pendulum present, current, right version |
| `pendulum.pendant`, `.armed`, `.motion`, `.fault` | out | bit | Status |
| `pendulum.owner` | out | s32 | Owning group, −1 none |
| `pendulum.<x|y|z|a>-vel` | out | float | −1…+1 |
| `pendulum.<axis>-steps` | out | s32 | Accumulated D-pad steps |
| `pendulum.<axis>-jog-counts` | out | s32 | Stick motion integrated + steps, in `jog-scale` units, for MPG-style jogging |
| `pendulum.btn-*` | out | bit | Buttons |
| `pendulum.jog-scale` | in | float | Units per count (default 0.0001 in) |
| `pendulum.max-speed` | in | float | Units/s at full stick (default 1.0 in/s = 60 IPM) |
| `pendulum.step-size` | in | float | Units per D-pad step (default 0.001 in) |

### Jogging method

Verified against probe.txt (PathPilot v2.14.3, LinuxCNC 2.8.0~pre1) and the
LinuxCNC 2.8 motion source:

- PathPilot jogs from its Python GUI via `tormach.*` pins (ShuttleXpress via
  `milljogtranslator`, operator console via `tormach.console-*`). Motion's
  hand-wheel inputs `axis.0-2.jog-counts` are unconnected; Pendulum drives
  them. The ShuttleXpress keeps working.
- `pendulum.py` integrates stick velocity into counts (`jog-scale` 0.0001 in)
  and adds D-pad steps (`step-size`). `axis.N.jog-vel-mode = 1`: motion drops
  any command beyond its stopping distance, so releasing the stick stops promptly.
- Motion only wheel-jogs in free (manual) mode with the machine on, not while
  homing or keyboard-jogging, not at zero feed override, and never past soft
  or hard limits. It saves new counts **before** checking `jog-enable`, so
  counter jumps while disabled are ignored.
- `attach.sh` forces `axis.N.jog-enable` off, loads the component and the
  counter nets, waits, then nets `pendulum.connected` to the enables.
- Fail-safe by construction: if the component dies, counts stop changing and
  motion stops. (halui analog jog was rejected: a dead component would leave a
  continuous jog running.)
- Open: LinuxCNC ignores wheel jogs in teleop mode; PathPilot may use teleop
  after referencing. First test checks referenced and unreferenced. Plan B is
  feeding PathPilot's own `tormach.jog-*` inputs.
- Open: motion moves each wheel step at full axis velocity; at very low stick
  speeds that may feel stepped. `axis.N.jog-max-vel` (Tormach-specific, default 0)
  is set to `MAX_JOG_VELOCITY_UPS`; its exact effect is to be observed.

## 8. Update / recovery flow

| Situation | Steps |
|---|---|
| PathPilot updated | Hold Select+Start 3 s → PENDULUM drive appears → `bash /media/$USER/PENDULUM/install.sh` → replug Pendulum |
| New firmware | Hold Select+Start+L+R 3 s (or BOOTSEL) → drag `pendulum.uf2` onto RPI-RP2 |
| Remove | `~/pendulum/uninst.sh` |

## 9. Bring-up plan

1. Build in VS Code, flash UF2, open the serial console: banner appears.
2. Plug in the 8BitDo, run `pad`: confirm "XInput". If it shows HID instead, `log on` and set `hid.*` offsets.
3. `log on`, wiggle sticks: confirm signs (stick up = positive Y).
4. On the controller: config mode → `install.sh` → send back `~/pendulum/probe.txt`.
5. Finalize `pendulum.hal` from the probe.
6. First motion test: feed override low, `max-speed 0.25`, hand on E-stop, Z clear of the work. Test each axis, then unplug the pendant mid-jog and Pendulum mid-jog: both must stop.
7. Raise `max-speed`; decide on deadman.

## 10. Open questions

1. ~~8BitDo SN30 Pro wired: XInput or HID?~~ Resolved: XInput, `045e:028e`, standard 4-interface Xbox 360 layout. It sends no input unless the host reads the language and manufacturer/product/serial strings (as Windows/Linux do); TinyUSB 0.18 (in Pico SDK 2.1.x) skips these; v0.2.0 builds against TinyUSB 0.21.0, which reads them during enumeration, and also re-reads them Windows-style after mount. Do not re-send SET_CONFIGURATION: that silences the pad. Streams a report every 4 ms once running.
2. PathPilot's LinuxCNC version and jog pin names (probe).
3. Does PathPilot run XDG autostart entries? If not, `watch.sh` needs another start hook (probe shows the session type).
4. Can `pendulum.step-size` follow PathPilot's selected increment (`tormach.jog-gui-step-index`)?
5. Real USB PID before building more than one.
6. `run` from config mode didn't return to run mode on v0.1.0 (unplug/replug works). v0.1.1 drops off USB cleanly before rebooting; retest.
