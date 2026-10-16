# Pendulum

> [!NOTE]
> This is in development and is only tested as far as the gamepad>rpi. 

Gamepad → PathPilot jog pendant adapter on a Raspberry Pi Pico W. Pendulum holds all the mapping and safety logic; PathPilot gets a small HAL component that is installed from a USB drive Pendulum itself provides. Full design: [docs/SPEC.md](docs/SPEC.md).

```
firmware/    Pico SDK project (Pendulum)
pathpilot/   files served on Pendulum's drive and installed on the controller
docs/        spec
```

## Build in VS Code

1. Install the **Raspberry Pi Pico** extension (publisher: Raspberry Pi). It downloads the Pico SDK and ARM compiler for you.
2. In the extension's panel choose **Import Project** and pick the `firmware` folder. Choose SDK **2.1.1** and board **pico_w**.
3. Click **Compile Project**. The first build also downloads two libraries (Pico-PIO-USB, tusb_xinput) from GitHub.
4. Flash: hold BOOTSEL while plugging in the Pico, then drag `firmware/build/pendulum.uf2` onto the **RPI-RP2** drive (or use **Run Project** with the Pico in BOOTSEL mode).

The `tusb_xinput` dependency is pinned to the version that matches TinyUSB 0.18, which SDK 2.1.1 ships. A newer SDK may need the newer commit (see `firmware/CMakeLists.txt`).

## Serial console

Plug Pendulum into any computer and open its serial port (VS Code: **Serial Monitor**, 115200, any line ending). Type `help`.

## Tests

The jog logic is plain C and runs on a PC:

```
cd firmware
gcc test/test_jog.c src/jog.c -Isrc -lm -o test_jog && ./test_jog
```

## Install on PathPilot

Hold **Back + Start** on the pendant for 3 s. Pendulum reboots as a drive named PENDULUM. On the controller:

```
bash /media/$USER/PENDULUM/install.sh
```

Then replug Pendulum. A fresh install loads the component but does **not** move the machine until `pendulum.hal` is finalized from `~/pendulum/probe.txt` (see spec §7).
