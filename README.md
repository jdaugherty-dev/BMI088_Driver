# BMI088 on Raspberry Pi 4 (SPI)

Bring-up driver for the BMI088 breakout board (`../breakoutboard`) on a
Raspberry Pi 4 Model B. Uses the official
[Bosch BMI08x_SensorAPI](https://github.com/boschsensortec/BMI08x_SensorAPI)
(vendored under `external/`, files taken as-is from the upstream repo) plus a
small Linux `spidev` + `libgpiod` platform layer written for this board.

## Wiring (from the schematic/PCB netlist)

The board plugs onto the full 40-pin GPIO header. The relevant nets, as routed:

| BMI088 signal | Net name | Pi header pin | Pi function     |
|----------------|----------|----------------|------------------|
| SCL/SCK        | SCK      | 23             | GPIO11 (SPI0 SCLK) |
| SDA/SDI        | MOSI     | 19             | GPIO10 (SPI0 MOSI) |
| SDO1/SDO2      | MISO     | 21             | GPIO9  (SPI0 MISO) |
| CSB1 (accel)   | CS1      | 18             | GPIO24 (manual CS)  |
| CSB2 (gyro)    | CS2      | 16             | GPIO23 (manual CS)  |
| PS             | -        | -              | tied to GND on the board (selects SPI for the gyro die) |
| VDD/VDDIO      | +3V3     | 1              | 3.3V |
| GNDA/GNDIO     | GND      | 39             | GND |

Note that `CS1`/`CS2` are **not** on the Pi's hardware `CE0`/`CE1` lines (pins
24/26), so `spidev`'s automatic chip-select is disabled and the driver drives
GPIO23/GPIO24 by hand around every SPI transfer. `SCK`/`MOSI`/`MISO` do land
on the Pi's real SPI0 bus, so hardware SPI is used for the actual clocking.

`INT1`-`INT4` land on header pins 8, 10, 12, 7 (GPIO14/15/18/4). This driver
polls the data-ready status registers over SPI instead of using the physical
interrupt lines, so they're unused for now — just note that `INT1`/`INT2`
sit on the Pi's UART TXD/RXD pins, which matters if you ever wire them up
(you'd need the Pi's serial console/login shell disabled on that UART first).

## One-time Raspberry Pi setup

```bash
sudo raspi-config   # Interface Options -> SPI -> Enable
sudo apt update
sudo apt install -y cmake build-essential gpiod libgpiod-dev
```

Reboot (or at least re-login) after enabling SPI so `/dev/spidev0.0` shows up.

By default only `root` can open `/dev/spidev0.0` and `/dev/gpiochip0` for
line requests; either run the program with `sudo`, or add your user to the
`spi` and `gpio` groups and log out/in again.

## Build & run (on the Pi)

Copy this `rpi_driver` folder to the Pi (e.g. `scp -r`, or `git` if you put
it under version control), then:

```bash
cd rpi_driver
cmake -B build
cmake --build build
sudo ./build/bmi088_test
```

Expected output on success:

```
accel chip id: 0x1E, gyro chip id: 0x0F
sample_count, ax_mps2, ay_mps2, az_mps2, gx_dps, gy_dps, gz_dps
0, 0.123, -0.045, 9.812, 0.01, -0.02, 0.00
...
```

Chip ID `0x1E` is the BMI088 accelerometer die; a gyro ID != `0x00`/`0xFF`
confirms the gyro die is also responding. `0x00`/`0xFF`/garbled data usually
means a wiring or chip-select problem (double check CS1/CS2 aren't swapped,
and that PS is actually grounded on your assembled board, not just in the
schematic).

Ctrl+C to stop; the gpiod line request and spidev fd get released on exit.

## Troubleshooting: `bmi08xa_init failed: -3`

`-3` is `BMI08_E_DEV_NOT_FOUND`. `bmi08xa_init()` read the accel's chip-ID
register over SPI and didn't get back `0x1E` -- this is a communication
failure, not a code path you can fix by changing settings in `main.c`. The
spidev/gpiod setup succeeding (you got this far instead of a
`platform init failed`/`perror` message) just means the Pi side opened the
bus and requested the GPIO lines fine; it says nothing about whether the
chip on the other end is actually answering.

Build and run the raw diagnostic, which reads the chip-ID register directly
and prints the actual byte instead of just pass/fail:

```bash
cmake --build build
sudo ./build/spi_diag
```

What the output means:

| Result | Likely cause |
|---|---|
| `0x1E` (accel) / `0x0F` (gyro) | that die is fine -- if only one side fails, focus wiring checks on that side's CS line only |
| `0x00` | MISO stuck low: no response at all from that CS. Check 3.3V is actually present at VDD/VDDIO *at the chip* (not just at the header), and that die's CS line actually reaches the chip |
| `0xFF` | MISO floating/open: SCK/MOSI/CS are probably not making contact, or MISO itself isn't connected |
| anything else, nonzero | something is answering, just not with the right ID -- check for CS1/CS2 swapped, or SCK/MOSI/MISO swapped |

Given this is a hand-soldered QFN breakout, roughly in order of likelihood:

1. **Cold/bridged solder joints on the QFN.** By far the most common cause
   for a first bring-up of a fine-pitch hand-soldered part. Reflow/reflow-check
   the accel and gyro CS pins (14 and 5), SCK/MOSI/MISO (8/9/10 and 15), and
   VDD/VDDIO/GND pins especially.
2. **Header not fully/correctly seated**, or off-by-one-row on the 40-pin
   header (check pin-1 alignment against the silkscreen).
3. **PS pin not actually grounded** on the physical board (it's correct in
   the schematic/netlist, but that doesn't guarantee the trace/joint is good).
4. **CS1/CS2 swapped** at the header-to-breadboard/jumper stage, if you're
   not going directly header-to-header.
5. **SPI not actually enabled**, or wrong `/dev/spidev*` device -- confirm
   with `ls /dev/spidev*` (should show `/dev/spidev0.0`).
6. **GPIO23/24 already claimed by something else** (another overlay, or a
   leftover process) -- `gpioinfo gpiochip0` lists every line's consumer;
   lines 23 and 24 should show as unused when nothing's running.

## Live 3D viewer

`viewer/imu_viewer.py` is a small cross-platform (run it on your dev machine,
not the Pi) viewer that shows the sensor's coordinate frame rotating/moving
live, plus bar gauges for XYZ translation and roll/pitch/yaw.

The Pi program streams one UDP packet per sample (raw accel + gyro, no
processing) to the viewer; the viewer does the sensor fusion. That split
means you can tweak the filter in Python and just re-run it, instead of
recompiling and redeploying to the Pi every time.

On your dev machine:

```bash
cd viewer
pip install -r requirements.txt
python imu_viewer.py --port 5005
```

On the Pi, point it at your dev machine's IP:

```bash
./build/bmi088_test 192.168.1.42 5005
```

(`192.168.1.42` is wherever `imu_viewer.py` is running — find it with
`ipconfig`/`ifconfig`. Your firewall needs to allow inbound UDP on that port.)

The left pane draws the IMU's body X/Y/Z axes (red/green/blue) rotated and
translated according to the fused estimate, against a faint gray reference
frame fixed at the origin. The right panes are horizontal bar gauges for
position (m) and roll/pitch/yaw (deg). Press `r` in the plot window to zero
position/velocity/yaw back to the origin.

Two things are inherent to a 6-DOF IMU (accel + gyro only, no magnetometer,
no GPS/vision) and not bugs:

- **Yaw drifts.** There's nothing absolute to correct heading against, so
  yaw is pure gyro integration and will wander over time.
- **Position drifts, sometimes a lot.** It's double-integrated acceleration
  with a simple zero-velocity-update heuristic to tame drift while the
  sensor is still. Treat it as "does the motion look roughly right," not
  as a real dead-reckoning position.

## What's next

This is intentionally a bring-up test (polling, default ranges, no FIFO, no
interrupts) so you can confirm the hardware and SPI wiring work end to end.
Once you've got clean accel/gyro numbers, you'll likely want to move reading
into whatever process is doing IMU/LiDAR fusion for the scanner, probably at
a higher ODR and/or using the FIFO instead of per-sample polling.
