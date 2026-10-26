# Pendulum — notes for Claude sessions

Gamepad → Tormach PathPilot jog pendant adapter. A Raspberry Pi Pico W sits
between a USB gamepad and the PathPilot controller and is the only place
behaviour lives; PathPilot gets a small fixed HAL component. Owner: Ian
(IRW Manufacturing). Machine: Tormach PCNC 1100, X/Y/Z.

Read first: `docs/SPEC.md` (design, report contract, config keys, safety
rules) and `docs/DEVLOG.md` (what was tried, what broke, why things are the
way they are).

## Layout

```
firmware/                 Pico SDK project (VS Code "Raspberry Pi Pico" extension, SDK 2.1.1, board pico_w)
  src/main.c              core 0: USB device, 8 ms jog loop, reboot/mode handling
  src/pad.c               core 1: PIO-USB host, XInput + HID input, 8BitDo handshake, diagnostics
  src/jog.c / jog.h       pure jog logic (no hardware calls) — unit tested on a PC
  src/config.c / .h       settings in last flash sector; bump CFG_LAYOUT when the struct changes
  src/console.c           serial console commands + hotkeys
  src/usb_descriptors.c   run mode (HID+CDC) / config mode (MSC+CDC)
  src/msc_disk.c          read-only installer drive, image built by tools/mkdisk.py from ../pathpilot
  src/pendulum.h          version (PENDULUM_FW_VERSION), report struct, shared types
  lib/tusb_xinput/        vendored XInput host driver (MIT), local changes marked "Pendulum"
  test/test_jog.c         host-side tests for jog.c
pathpilot/                files served on the PENDULUM drive and installed on the controller (8.3 lowercase names only)
docs/                     SPEC.md, DEVLOG.md
```

## Commands

```sh
# unit tests (run after any jog.c / jog.h change)
cd firmware && gcc -Wall -Wextra test/test_jog.c src/jog.c -Isrc -lm -o /tmp/test_jog && /tmp/test_jog

# CLI build (VS Code extension does the same)
cd firmware && mkdir -p build && cd build && cmake .. && make -j8   # -> build/pendulum.uf2
```

First configure downloads TinyUSB 0.21.0 and Pico-PIO-USB via FetchContent.

## Hard-won facts — don't undo these

- **TinyUSB is pinned to 0.21.0** (not the SDK's 0.18) via `PICO_TINYUSB_PATH`
  in `firmware/CMakeLists.txt`. 0.19+ reads the language/manufacturer/product/
  serial strings during enumeration before SET_CONFIGURATION. The 8BitDo
  SN30 Pro (045e:028e, XInput) sends **no input** without those reads.
- **Never send SET_CONFIGURATION again after enumeration.** v0.1.7 did; the pad
  sent one packet and went silent. `pad.c` re-reads strings Windows-style
  (wLength 255) after mount as a backup, with no re-config.
- Pico-PIO-USB is pinned to the commit TinyUSB 0.21.0 pins (675543b). Keep
  TinyUSB / PIO-USB / tusb_xinput versions matched.
- The XInput driver only forwards `00 14 ...` input reports; `tuh_xinput_raw_cb`
  (our hook) sees every packet. `pad` shows raw/report/error counters.
- Wired 360-style pads stream a report every 4 ms once running. Stick values
  on this pad are 8-bit (low byte always 00).
- Sticks overshoot past centre on release → spring-back guard (`reverse_ms`).
- Mouse/keyboard on the gamepad port is a valid test that PIO-USB host input works.

## The PathPilot controller (from probe 1)

- PathPilot v2.14.3, Linux Mint 17.3, kernel 4.11.9-rt7, x86_64, user `operator`.
- LinuxCNC 2.8.0~pre1 runs in place from `~/tmc` (symlink to `~/v2.14.3`);
  `halcmd` is `~/tmc/bin`, `hal` module `~/tmc/lib/python`, PathPilot's python
  is `~/v2.14.3/venv/bin/python`. No rip-environment script. Always
  `. ppenv.sh` first (copies milltask's env, else builds it from `~/tmc`).
- ini: `~/tmc/configs/tormach_mill/tormach_1100-3_7i92.ini`; halui loaded;
  real ShuttleXpress wired via `millshuttlexpress.hal` (postgui).
- `pathpilot/` file names must stay lowercase 8.3 (FAT drive image).

## Safety rules (keep them)

- Stopping is never debounced or delayed; starting is (`start_ms`, `step_ms`).
- Plug-in lockout: no motion until all sticks/buttons/triggers seen neutral.
- Holding Back suppresses all motion (mode combos).
- Unplug / stale data / wrong protocol version → zero velocity (box and PC side).
- Jogging uses motion's hand-wheel inputs (`axis.N.jog-counts`), velocity
  mode, enables wired last by `attach.sh`. Don't switch to halui analog jog
  (not fail-safe if the component dies). In bash, quote `'=>'` in halcmd lines.
- Report contract changes bump `PENDULUM_PROTOCOL_VERSION` and `pendulum.py` together.

## Workflow

- Ian commits and pushes (GitHub: ianwinsemius/pendulum, private). Claude
  sessions may not be able to push; hand over changes as files in the working
  tree, uncommitted, and say so.
- Every firmware change: bump `PENDULUM_FW_VERSION`, run tests, build, and
  update the Status line in `docs/SPEC.md`. Add a DEVLOG entry for anything
  learned the hard way.
- Hardware testing is done by Ian over the serial console
  (`screen /dev/cu.usbmodem* 115200` on his Mac). Ask for `pad` output and the
  plug-in dump when debugging the gamepad side.

## Status / next

- Working on hardware: boot, console, config-mode drive, 8BitDo input, debounce,
  spring-back guard, jog monitor (Ctrl-G).
- First motion achieved (v0.3.0, unreferenced). v0.3.1: free mode default,
  sticks follow the table, D-pad follows stick inversion.
- v0.4.0: Y cycles PathPilot's step (relay of tormach.jog-step-button), D-pad
  follows PathPilot's increment, LEDs 1-4 show it (host→Pendulum HID output
  report). max-speed default 0.667 (40 IPM).
- Next: A (cycle start, hold 2 s), B (reset), X (stop or feed hold) once
  ui_common.py console handling is understood; referenced jog test; unplug
  tests; −X stuck while sweeping (spring-back guard?).
- Open: `run` from config mode didn't return to run mode on v0.1.0 (replug
  works); retest. Real USB PID before building more than one.
