# Pendulum

> [!NOTE]
> This is in development and is only tested as far as the gamepad>rpi. 

Gamepad → PathPilot jog pendant adapter on a Raspberry Pi Pico W. Pendulum holds all the mapping and safety logic; PathPilot gets a small HAL component that is installed from a USB drive Pendulum itself provides. Full design: [docs/SPEC.md](docs/SPEC.md). Debugging history and decisions: [docs/DEVLOG.md](docs/DEVLOG.md). Notes for AI coding sessions: [CLAUDE.md](CLAUDE.md).

```
firmware/    Pico SDK project (Pendulum)
pathpilot/   files served on Pendulum's drive and installed on the controller
docs/        spec
```

## Build in VS Code

1. Install the **Raspberry Pi Pico** extension (publisher: Raspberry Pi). It downloads the Pico SDK and ARM compiler for you.
2. In the extension's panel choose **Import Project** and pick the `firmware` folder. Choose SDK **2.1.1** and board **pico_w**.
3. Click **Compile Project**. The first build also downloads TinyUSB 0.21.0 and Pico-PIO-USB from GitHub (a minute or two).
4. Flash: hold BOOTSEL while plugging in the Pico, then drag `firmware/build/pendulum.uf2` onto the **RPI-RP2** drive (or use **Run Project** with the Pico in BOOTSEL mode).

The build uses its own pinned TinyUSB (0.21.0) instead of the one inside the Pico SDK, because the older one skips a startup step the 8BitDo needs. The XInput driver is vendored in `firmware/lib/tusb_xinput`.

## Serial console

Plug Pendulum into any computer and open its serial port (VS Code: **Serial Monitor**, 115200, any line ending). Type `help`.

## Tests

The jog logic is plain C and runs on a PC:

```
cd firmware
gcc test/test_jog.c src/jog.c -Isrc -lm -o test_jog && ./test_jog
```

## Install on PathPilot

Hold **Select + Start** on the pendant for 3 s. Pendulum reboots as a drive named PENDULUM. On the controller:

```
bash /media/$USER/PENDULUM/install.sh
```

Then replug Pendulum. A fresh install loads the component but does **not** move the machine until `pendulum.hal` is finalized from `~/pendulum/probe.txt` (see spec §7).
