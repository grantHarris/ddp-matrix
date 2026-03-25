# ddp-matrix

`ddp-matrix` is a Raspberry Pi DDP receiver for HUB75 RGB panels. It listens for
DDP pixel data on UDP port `4048`, copies the incoming RGB data into a frame
buffer, and renders completed frames to LED matrix panels using
[`rpi-rgb-led-matrix`](https://github.com/c-base/rpi-rgb-led-matrix).

The receiver is intended to run directly on a Pi that is physically connected to
the panel. It works well with DDP senders such as xLights, WLED, LEDfx, and
other software that outputs standard DDP RGB payloads.

## Features

- Receives DDP over UDP on port `4048`
- Supports partial packet updates via DDP `data_offset`
- Renders completed frames on `DDP_PUSH`
- Uses `rpi-rgb-led-matrix` as a Git submodule
- Uses CMake to generate both runtime defaults and the installed `systemd` unit

## Configuration Model

The project now uses CMake cache variables as the source of truth for both:

- The receiver's compiled-in defaults
- The `ExecStart=` arguments written into the generated `systemd` service

The main variables are:

- `DDP_MATRIX_GPIO_MAPPING`
- `DDP_MATRIX_PANEL_ROWS`
- `DDP_MATRIX_PANEL_COLS`
- `DDP_MATRIX_PANEL_CHAIN`
- `DDP_MATRIX_PANEL_PARALLEL`
- `DDP_MATRIX_BRIGHTNESS`
- `DDP_MATRIX_GPIO_SLOWDOWN`
- `DDP_MATRIX_ENABLE_REALTIME`
- `DDP_MATRIX_REALTIME_PRIORITY`
- `DDP_MATRIX_EXTRA_ARGS`

Panel geometry is expressed explicitly through the panel pixel size and panel
counting variables:

- `DDP_MATRIX_PANEL_ROWS`
- `DDP_MATRIX_PANEL_COLS`
- `DDP_MATRIX_PANEL_CHAIN`
- `DDP_MATRIX_PANEL_PARALLEL`

From those, CMake also derives and writes out:

- Total panel count
- Total canvas width
- Total canvas height

## Repository Setup

Clone the repo with submodules:

```bash
git clone --recurse-submodules https://github.com/grantHarris/ddp-matrix.git
cd ddp-matrix
```

If you already cloned it without submodules:

```bash
git submodule update --init --recursive
```

The matrix library lives at:

- `rpi-rgb-led-matrix`

and is configured as a submodule pointing at:

- `https://github.com/c-base/rpi-rgb-led-matrix`

## Configure And Build

Configure the project:

```bash
cmake -S . -B build
```

Build the receiver:

```bash
cmake --build build
```

The top-level `Makefile` is now only a thin wrapper over that flow, so `make`
and `make install` still work if you prefer them.

Example configuring a `2 x 64x64` panel setup:

```bash
cmake -S . -B build \
  -DDDP_MATRIX_PANEL_ROWS=64 \
  -DDDP_MATRIX_PANEL_COLS=64 \
  -DDDP_MATRIX_PANEL_CHAIN=2 \
  -DDDP_MATRIX_PANEL_PARALLEL=1 \
  -DDDP_MATRIX_BRIGHTNESS=100 \
  -DDDP_MATRIX_GPIO_SLOWDOWN=2
```

During configure, CMake prints the derived panel count, canvas geometry, and
the exact service arguments it will install.

## Run Manually

The binary generally needs to run as `root` for GPIO access. If you run the
binary manually without arguments, it will use the defaults compiled from your
current CMake configuration:

```bash
sudo ./build/ddp-receiver
```

Example with explicit matrix settings:

```bash
sudo ./build/ddp-receiver \
  --led-gpio-mapping=adafruit-hat-pwm \
  --led-rows=64 \
  --led-cols=128 \
  --led-brightness=100
```

## Install As A Service

Install the binary to `/opt`, install the generated `systemd` unit, reload
`systemd`, enable the service, and restart it:

```bash
sudo cmake --install build
```

Or with the wrapper:

```bash
sudo make install
```

This performs the following by default:

- Copies the binary to `/opt/ddp-receiver`
- Installs `ddp-receiver.service` to `/etc/systemd/system/ddp-receiver.service`
- Runs `systemctl daemon-reload`
- Runs `systemctl enable ddp-receiver.service`
- Runs `systemctl restart ddp-receiver.service`

The installed service file is generated from `ddp-receiver.service.in`, and its
`ExecStart=` line is built from the CMake variables above.

If you want to install files without touching `systemd`, configure with:

```bash
cmake -S . -B build -DDDP_MATRIX_MANAGE_SYSTEMD_SERVICE=OFF
```

## DDP Frame Layout

The receiver treats incoming pixel data as contiguous 24-bit RGB bytes for the
full matrix canvas in row-major order:

- Pixel `(0, 0)` starts at byte `0`
- Each pixel is `R`, `G`, `B`
- Offset calculation is `(y * width + x) * 3`

DDP packets can update any portion of that buffer using the header
`data_offset`, and a packet with the DDP push flag triggers the frame swap.

## Development Notes

- The legacy top-level `Makefile` now delegates to CMake
- The build output lives in `build/` by default
- The matrix library is tracked as a submodule, not vendored source
