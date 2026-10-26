# Output Expander

Firmware for the Mekatrol eight-channel output expansion boards.

See [Project setup](docs/PROJECT_SETUP.md) for toolchain, udev, and wiring
instructions, and [Building](docs/BUILD.md) for build, test, and flash commands.

The boards use the WCH CH32V003 RISC-V microcontroller and are available with
two output-stage variants:

- Triac outputs for switching AC loads.
- FET outputs for switching DC loads.

The firmware in `src/main.rs` targets the CH32V003F4P6 and turns LED1 on for
500 ms and off for 500 ms.

## LED1 test wiring

The evaluation-board schematic does not permanently connect LED1 to the MCU.
LED1's cathode is exposed on P4 pin 1. Add this jumper before running the
firmware:

| From | To |
| --- | --- |
| P4 pin 1 (`LED1`) | P2 pin 14 (`PC1`, bottom-right pin) |

LED1 is wired from VCC through R2 to the `LED1` net, so it is active-low.

## Rust setup

The CH32V003 uses the RV32EC instruction set. The checked-in
`rust-toolchain.toml` selects nightly Rust with `rust-src`, and the checked-in
JSON target describes RV32EC. From this directory, install the toolchain by
running any Cargo command, then install the WCH-LinkE flashing utility:

```sh
cargo install --git https://github.com/ch32-rs/wlink
```

Build the LED firmware:

```sh
cargo firmware
```

## Flashing with WCH-LinkE

Power the board from USB-C with S2 set to ON. Connect WCH-LinkE SWDIO to P2
PD1 (row 3, right) and GND to P2 GND (row 2, either pin). Leave the WCH-LinkE
3V3 pin disconnected because the target is separately powered.

With the programmer attached, build and flash in one step:

```sh
cargo flash
```

The `firmware` and `flash` aliases build `core` for RV32EC. The Cargo runner
then passes the generated ELF file to `wlink -v flash`.
