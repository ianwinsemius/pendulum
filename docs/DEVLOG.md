# Pendulum dev log

Newest last. What was tried, what the hardware showed, what changed. Version
numbers match `PENDULUM_FW_VERSION`.

## Design decisions (before code)

- Started as "make a $20 gamepad a PathPilot pendant". Options considered:
  keyboard emulation (no variable speed), ShuttleXpress emulation (works with
  PathPilot's existing `shuttle` HAL driver, VID 0b33:0020, 5-byte report —
  confirmed from LinuxCNC `shuttle.c`), or a custom HAL component.
- Chose a **configurable intermediary**: all mapping, curves and safety in the
  box; PathPilot gets one fixed HAL component and wiring file. The box carries
  its own installer (config-mode USB drive) so a PathPilot update only needs a
  re-run of `install.sh`.
- ShuttleXpress emulation allows one ring + one wheel, so true X+Y diagonals
  need our own HAL component. Default arbitration is "planar" (X+Y together,
  Z alone).
- Gutting the gamepad (RP2040 inside the shell) was considered and rejected:
  loses swap-any-pendant modularity.
- Board: Raspberry Pi Pico W (FCC ID 2ABCB-PICOW). Native USB → controller;
  gamepad on a USB-A jack via Pico-PIO-USB (D+ GP0, D− GP1, VBUS pin 40, GND pin 3).
- Name: "ppbox" → **Pendulum** (autocorrect's suggestion, kept).

## 0.1.0 — first hardware boot

- Flashed fine; console up; config mode serves the PENDULUM drive.
- `screen` shows nothing until a newline; banner wording "type 'help'" was
  taken literally. `config` drops the session (expected — it reboots).
- `run` from config mode reportedly did not return to run mode. Unresolved;
  0.1.1 added a clean USB disconnect before reboot. Replug always works.

## 0.1.1 – 0.1.5 — 8BitDo enumerates but sends nothing

- 8BitDo SN30 Pro wired enumerates as `045e:028e` (wired Xbox 360), XInput
  type 3. Player-1 LED lights (our LED command arrives).
- `pad` diagnostics: `reading=yes raw=0 reports=0 errors=0` — endpoint 0x81
  armed, every poll NAKed.
- 0.1.2: removed blocking LED/rumble writes from inside the mount callback.
  No change.
- 0.1.3: added Linux-xpad wake request (class GET_REPORT 0x0100, STALLs) and
  Windows vendor request 0xC1/0x01/0x0100 (returns the 20-byte capabilities
  report `00 14 ff f7 ...`). No change.
- 0.1.4: vendored tusb_xinput, added raw packet hook. Still `raw=0`.
- 0.1.5: dumped device + config descriptors. Textbook 360 layout:
  itf0 ff/5d/01 (EP 81 IN 32B @4, EP 02 OUT), itf1 ff/5d/03, itf2 ff/5d/02,
  itf3 ff/fd/13 (security). Driver binds the correct interface/endpoint.
- **Mouse on the gamepad port streams fine** → PIO-USB host is OK; problem is
  the 8BitDo's expectations of the host.

## 0.1.6 – 0.1.7 — string reads unlock it; re-config breaks it

- Found: some 8BitDo pads check the host enumeration sequence and stay silent
  unless the language + manufacturer/product/serial strings are read before
  SET_CONFIGURATION (Windows and Linux do; TinyUSB 0.18 does not).
  Ref: EinTim23/hiddriver360 issue #122.
- 0.1.6: after mount, read strings (wLength 255) then send SET_CONFIGURATION
  again. **Pad streamed input** (and buzzed on connect).
- 0.1.7: added quieter logging + jog monitor. Pad went silent again: log
  showed exactly one input packet arriving right after the string reads,
  then nothing after the second SET_CONFIGURATION. Conclusion: strings are
  the unlock; the re-config is harmful (0.1.6 worked by timing luck).

## 0.2.0 — TinyUSB 0.21.0

- TinyUSB 0.19+ reads strings during enumeration, before SET_CONFIGURATION.
  Pinned TinyUSB 0.21.0 (`PICO_TINYUSB_PATH`), matching Pico-PIO-USB
  (675543b), tusb_xinput API update (`open` returns uint16_t). Kept the
  Windows-style string re-read after mount, removed the re-config.
- Result: reliable input. Full range ±32767, centre exactly 0, up = positive,
  buttons as XInput bits, triggers digital (0/255), multi-button chords fine.

## 0.2.1 — spring-back guard

- Fast stick releases overshoot past centre (−31744 → +17152 → −4096 → 0),
  which would cause a brief reverse jog. Guard: motion opposite to the last
  driven direction is ignored until the stick rests at centre `reverse_ms`
  (100 ms). Tests use the real sample values.

## 0.2.2 — debounce + hotkeys

- Ian: "a finger slip is a reasonable thing to expect." Start of motion is
  debounced (`start_ms` 80 ms sticks, `step_ms` 40 ms D-pad/bumpers); stop is
  never debounced. Lockout/ownership use raw deflection.
- Console hotkeys: Ctrl-G jog monitor, Ctrl-L pendant log, Ctrl-P status,
  Ctrl-C quiet.
- Ian confirmed on hardware: debounce works well.

## 0.2.3 — first look at the real controller

- First `install.sh` run: "no Python here can import 'hal'"; `halcmd` not on
  PATH. probe 1 showed why: **PathPilot v2.14.3** (Linux Mint 17.3,
  4.11.9-rt7, x86_64) runs LinuxCNC run-in-place from `~/tmc`
  (`~/tmc/scripts/start_linuxcnc`, ini
  `~/tmc/configs/tormach_mill/tormach_1100-3_7i92.ini`). Its PATH/PYTHONPATH
  only exist inside PathPilot's process tree.
- Fix: `ppenv.sh` copies PATH, PYTHONPATH, LD_LIBRARY_PATH and LinuxCNC vars
  from the running `milltask` (same user, `/proc/<pid>/environ`), falling back
  to `~/tmc/scripts/rip-environment`. install/attach/probe/uninst all source it.
- Also learned: `halui` is loaded (`HALUI = halui`); a real ShuttleXpress is
  connected and wired by `POSTGUI_SHUTTLEXPRESS_HALFILE = millshuttlexpress.hal`;
  there is a VMC console halfile; operator has passwordless sudo; autostart
  entries in `~/.config/autostart` (pathpilot.desktop) so pendulum-watch works.
- probe 2 captures HAL pins/signals, the shuttle and postgui hal files, and
  ini jog/velocity limits.
- Labels: combos now say Select (8BitDo) instead of Back (XInput).

## 0.2.4 — environment fallback

- 0.2.3 install still said "environment: none". Ian ran the lookup by hand:
  milltask's environment is readable and complete (PATH has
  `~/v2.14.3/venv/bin` first, then `~/tmc/bin`; PYTHONPATH has
  `~/tmc/lib/python`; LD_LIBRARY_PATH `~/tmc/lib`; EMC2VERSION 2.8.0~pre1).
  `~/tmc/scripts/rip-environment` does **not** exist on PathPilot.
- Exact cause of the miss not pinned down (pgrep options on Mint 17.3's
  procps, or PathPilot not fully up). `ppenv.sh` now: looser pgrep, then a
  **static** environment built from the `~/tmc` layout (works with PathPilot
  closed), then rip-environment; and it records why (`PENDULUM_ENV_WHY`),
  shown by install.sh and probe.sh.
- Python preference: `python` first (PathPilot's venv), then python2/2.7/3.

## 0.2.5 — probe warning fixed

- 0.2.4's probe said "PathPilot doesn't seem to be running" while it was
  running: the warning keyed off how the environment was found (anything but
  "running"), not whether HAL answered. Now it tests `halcmd show comp`.
  Probe also lists the milltask processes (user/pid/args) to explain why the
  milltask lookup fell back to the static environment.

## 0.3.0 — real jog wiring

- probe 2 (PathPilot v2.14.3): PathPilot jogs in Python from `tormach.*` pins
  (shuttle → `milljogtranslator` → `tormach.jog-counts` / `jog-ring-speed-signed`;
  console MPG → `tormach.console-jog-counts`). `axis.0-3.jog-counts`,
  `jog-enable`, `jog-scale`, `jog-vel-mode`, `jog-max-vel` (Tormach addition)
  all unconnected. halui loaded, `halui.jog.N.analog` unconnected.
  Machine was in joint mode, unreferenced, during the probe.
- Chose motion hand-wheel inputs over halui analog jog: if the component dies,
  counts stop → motion stops; a halui continuous jog would keep running.
- LinuxCNC 2.8 `control.c` confirmed: old counts saved before the enable
  check (jumps while disabled ignored); vel-mode clamps the lead to the
  stopping distance; wheel jog needs free mode, machine on, not homing, no kb
  jog, feed scale > 0, inside jog limits; returns early in teleop.
- `pendulum.hal` now nets counts and sets scale/vel-mode/jog-max-vel (from ini
  via `halcmd -i`). `attach.sh` forces enables off → loads → nets counts →
  0.5 s → nets `pendulum.connected` to the enables. install.sh unloads an
  old component so the watcher reloads with new wiring; uninst.sh disables
  enables before unloading.
- Bug caught with a mock halcmd: unquoted `=>` in a bash `halcmd net` line is
  a shell redirect (would have dropped axis 0 and created a file). Quoted.
- First-run tuning: max-speed 0.25 in/s.
- pendulum.py tested under Python 2.7.15 (PathPilot has 2.7.6) with a fake hal module.

## 0.3.1 — first motion, free mode, table-following sticks

- **First motion on the PCNC 1100 (2026-10-25):** after reinstalling 0.3.0,
  `pendulum-ok` TRUE, sticks jog the machine. Unreferenced test.
- Slow at the 0.25 in/s first-run cap (expected); live tuning with
  `halcmd setp pendulum.max-speed …`.
- X and Y were "the wrong way round": stick right gave X+ (PathPilot/keyboard
  convention = table moves left). Ian wants sticks to follow the table.
  New defaults `map.lx -x`, `map.ly -y`; Z (stick up = spindle up) was right.
- D-pad/bumper steps now follow the stick inversion for their axis (they
  previously used fixed directions, so they'd have disagreed with the sticks).
- Ian wanted all three axes at once: new `mode free` (default) — every stick
  axis independent, steps always eligible. `planar`/`single` remain.
- CFG_LAYOUT 4 (saved settings reset to the new defaults once).

## 0.4.0 — follow PathPilot's step size, Y cycles it, LEDs show it

- First-motion feedback: slow at 0.25 in/s; Ian settled on 40 IPM →
  `pendulum.max-speed 0.667` default. −X sometimes "stuck" while working all
  three axes: likely the spring-back guard blocking a reversal swept through
  centre without a pause (test with `set reverse_ms 0`; pending).
- PathPilot step index measured on the machine: .0001=0 .001=1 .01=2 .1=3
  (`sxp.gui_step_index`, written by `tormach.jog-gui-step-index`).
- PathPilot changes step on a press-then-release of `tormach.jog-step-button`
  (ui_common.py ~2885).
- Component: `step-follow` (on) uses PathPilot's increment for D-pad steps
  (metric: steps off until mapped); Y press → 0.25 s pulse on
  `step-button-out`, OR'ed with the ShuttleXpress button (`shuttle-step-in`).
  attach.sh moves `tormach.jog-step-button` from `sxp.step-button` to
  `pendulum-step-out`; `ppwire.sh` (pendulum_unwire) restores it and forces
  enables off — used by attach/install/uninst. While pendulum isn't loaded the
  shuttle's step button works as before.
- Host → Pendulum channel: HID output report (8 bytes: version, led 0–4,
  rumble reserved) written to /dev/hidraw (report number 0 first). Firmware
  lights XInput player LED 1–4 = step index 0–3, re-applied after pad replug.
- Requested next: X = stop or feed hold (TBD), B = reset, A = cycle start held
  2 s (ignored while jogging, only when idle with a program loaded). Waiting
  on ui_common.py console handling (console_disconnected_flag, ~6000-6070).
