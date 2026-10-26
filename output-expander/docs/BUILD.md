# Building the Output Expander Firmware

## Firmware build

Run commands in this document from the `output-expander` directory.

Build the optimized CH32V003 firmware:

```sh
cargo firmware
```

`firmware` is a Cargo alias defined in `.cargo/config.toml`. It builds the
`src/main.rs` firmware in release mode and builds Rust's `core` library for the
custom RV32EC target.

The resulting flashable ELF file is:

```text
target/riscv32ec-unknown-none-elf/release/output-expander
```

The target specification enables the RISC-V embedded and compressed
instruction sets used by the CH32V003. The `ch32-hal` dependency supplies the
CH32V003F4P6 peripheral definitions, memory layout, and runtime support.

## Firmware behavior

The firmware configures PC1 as an active-low output. It turns LED1 on for 500
ms and off for 500 ms, giving a one-second period and 50 percent duty cycle.

The board requires a jumper from P4 pin 1 (`LED1`) to P2 pin 14 (`PC1`). See
[PROJECT_SETUP.md](PROJECT_SETUP.md) for the complete board and programmer
wiring.

## Flashing

Connect and power the board as described in
[PROJECT_SETUP.md](PROJECT_SETUP.md), then build and flash it with:

```sh
cargo flash
```

`flash` is a Cargo alias that performs a release build and invokes the
configured runner:

```text
wlink -v flash <firmware-elf>
```

A successful flash reports the attached CH32V003, programs flash memory,
resets the target, and resumes execution. LED1 should then blink continuously.

If `wlink` reports `failed to open device (errno 13)`, install the Linux udev
rule documented in [PROJECT_SETUP.md](PROJECT_SETUP.md), then unplug and
reconnect the WCH-LinkE. Do not use `sudo cargo flash`.

If `wlink` opens the programmer but cannot attach to the target, verify that:

- the board is powered through USB-C and S2 is ON;
- WCH-LinkE SWDIO is connected to P2 PD1;
- the programmer and board grounds are connected; and
- WCH-LinkE 3V3 is disconnected from the separately powered board.

## Clean builds

Cargo performs incremental builds by default. Remove all generated build
artifacts when a completely clean build is required:

```sh
cargo clean
cargo firmware
```

`cargo clean` removes only the generated `output-expander/target` directory.

