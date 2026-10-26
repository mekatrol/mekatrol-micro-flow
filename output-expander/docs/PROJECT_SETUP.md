# Output Expander Project Setup

## Required tools

Development requires:

- Git
- Rustup
- the Rust toolchain selected by `rust-toolchain.toml`
- `wlink` for flashing through a WCH-LinkE programmer

The project selects nightly Rust because the CH32V003 uses the RV32EC
instruction set. Rustup automatically installs the required `rust-src` and
Clippy components when a Cargo command is first run in `output-expander`.

Install Rustup using the instructions at <https://rustup.rs>, then verify it is
available:

```sh
rustup --version
cargo --version
```

On Debian, Ubuntu, and Linux Mint, install the native libraries needed to build
`wlink`:

```sh
sudo apt update
sudo apt install libudev-dev libusb-1.0-0-dev
```

Install `wlink` from its upstream repository:

```sh
cargo install --git https://github.com/ch32-rs/wlink
wlink --version
```

Ensure Cargo's binary directory, normally `$HOME/.cargo/bin`, is on `PATH` if
the shell cannot find `wlink` after installation.

## Linux WCH-LinkE permissions

The WCH-LinkE uses USB vendor ID `1a86` and product ID `8010`. Linux may detect
the programmer but deny access to a normal user with this error:

```text
Failed to open USB device: failed to open device (errno 13)
It's likely the udev rules are not installed properly.
```

Install a udev rule that grants the active desktop user access:

```sh
sudo tee /etc/udev/rules.d/60-wch-link.rules >/dev/null <<'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", MODE="0660", TAG+="uaccess"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and reconnect the WCH-LinkE after installing the rule. Do not run Cargo
or `wlink` with `sudo`; doing so can leave root-owned files in the project and
Cargo directories.

Use the following command to confirm that Linux sees the programmer:

```sh
lsusb -d 1a86:8010
```

## Programmer wiring

Power the CH32V003F4P6-R0-1v1 board through its USB-C connector and set S2 to
ON. The USB-C connector supplies power only.

Connect the WCH-LinkE to the P2 header as follows:

| WCH-LinkE | CH32V003 board P2 |
| --- | --- |
| SWDIO | PD1, row 3 right |
| GND | GND, row 2 either pin |

Leave the WCH-LinkE 3V3 pin disconnected while the target is powered from
USB-C. The CH32V003 uses its PD1 single-wire debug connection for programming.

## LED1 test wiring

The evaluation-board schematic exposes LED1 on P4 instead of permanently
connecting it to an MCU pin. Install this jumper for the firmware:

| From | To |
| --- | --- |
| P4 pin 1 (`LED1`) | P2 pin 14 (`PC1`, bottom-right pin) |

LED1 is active-low because it is wired from VCC through R2 to the LED1 net.

## Verify installation

Run these commands from `output-expander`:

```sh
rustc --version
cargo --version
wlink --version
cargo firmware
```

The final command must produce the CH32V003 firmware ELF without errors. See
[BUILD.md](BUILD.md) for build, test, and flash commands.

