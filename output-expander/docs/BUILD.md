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

The current firmware is a minimal half-duplex RS-485 echo test. USART1 uses
PD5 for TX and PD6 for RX at 19,200 baud, 8 data bits, no parity, and one stop
bit (8N1). Input is buffered until a newline is received (or the 64-byte
buffer fills), then the complete message is sent back unchanged after a short
RS-485 turnaround delay.

PC6 drives the transceiver's active-high DE input and PC7 drives its
active-low /RE input. Firmware deliberately gives both pins the same level:
LOW while receiving and HIGH while transmitting. This matches the planned
hardware revision where DE and /RE are tied together and driven by PC6. The
driver remains enabled until USART transmission-complete indicates that the
last stop bit has left the pin.

Configure the host adapter for 19,200 8N1 and disable local echo when testing.
This firmware is intentionally blocking; it is a physical-transport bring-up
step, not the final interrupt-driven transport or a Modbus implementation.

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
resets the target, and resumes execution. Bytes sent by an attached RS-485
host should then be returned unchanged.

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

