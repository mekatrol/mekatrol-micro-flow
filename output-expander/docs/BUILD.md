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

## Memory usage and stack headroom

Build the release firmware first, then use the LLVM tools shipped with the
nightly Rust toolchain to display its section sizes:

```sh
cargo firmware

ELF=target/riscv32ec-unknown-none-elf/release/output-expander
LLVM_TOOLS="$(rustc +nightly --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin"
"$LLVM_TOOLS/llvm-size" "$ELF"
"$LLVM_TOOLS/llvm-size" -A -x "$ELF"
```

The first command gives a compact summary, but its `data` column is not a
reliable SRAM total. `llvm-size` classifies the executable `.data.ws2812`
input as text even though the linker places it in SRAM. Interpret the compact
summary only as:

- Flash used = `text + data`. `data` counts here because its initial values are
  stored in Flash and copied to SRAM during startup.
- Static SRAM and maximum initial stack headroom must be calculated from the
  linker symbols `_data_vma`, `_ebss`, and `_stack_start`. The firmware has no
  heap.

The following command calculates those totals and percentages against the
CH32V003F4P6's 16 KiB Flash and 2 KiB SRAM capacities:

```sh
FW_FLASH_USED=$("$LLVM_TOOLS/llvm-size" "$ELF" | awk 'NR == 2 { print $1 + $2 }')
FW_RAM_BASE_HEX=$("$LLVM_TOOLS/llvm-nm" -n "$ELF" | awk '$3 == "_data_vma" { print $1 }')
FW_EBSS_HEX=$("$LLVM_TOOLS/llvm-nm" -n "$ELF" | awk '$3 == "_ebss" { print $1 }')
FW_STACK_TOP_HEX=$("$LLVM_TOOLS/llvm-nm" -n "$ELF" | awk '$3 == "_stack_start" { print $1 }')

FW_RAM_BASE=$((0x$FW_RAM_BASE_HEX))
FW_EBSS=$((0x$FW_EBSS_HEX))
FW_STACK_TOP=$((0x$FW_STACK_TOP_HEX))
FW_STATIC_RAM=$((FW_EBSS - FW_RAM_BASE))
FW_STACK_MAX=$((FW_STACK_TOP - FW_EBSS))

awk -v flash="$FW_FLASH_USED" -v ram="$FW_STATIC_RAM" -v stack="$FW_STACK_MAX" 'BEGIN {
    printf "Flash:               %4d / 16384 bytes (%5.1f%%), %d free\n", flash, 100 * flash / 16384, 16384 - flash
    printf "Static SRAM:         %4d /  2048 bytes (%5.1f%%)\n", ram, 100 * ram / 2048
    printf "RAM remaining after static/global variables: %d bytes\n", stack
    printf "Maximum available for runtime stack:         %d bytes\n", stack
}'
```

For example, the current firmware prints the remaining value explicitly—no
manual subtraction is required:

```text
Flash:               6976 / 16384 bytes ( 42.6%), 9408 free
Static SRAM:          788 /  2048 bytes ( 38.5%)
RAM remaining after static/global variables: 1260 bytes
Maximum available for runtime stack:         1260 bytes
```

`llvm-size -A -x` shows the individual sections and their addresses. Executable
sections such as `.vector_table` and `.text` should be Flash-addressed, while
`.data` and `.bss` should be in the `0x20000000` SRAM region. There must be no
allocated `.highcode` section.

To confirm the static SRAM boundary and initial stack pointer from linker
symbols, run:

```sh
"$LLVM_TOOLS/llvm-nm" -n "$ELF" | grep -E ' (_edata|_ebss|_stack_start)$'
```

On this MCU, `_stack_start` should be `0x20000800`, the top of its 2 KiB SRAM.
The stack grows downward toward `_ebss`. Static SRAM is `_ebss - _data_vma`,
and maximum initial stack headroom is `_stack_start - _ebss`; these symbol
calculations include executable code placed in `.data`.

This headroom is an upper bound, not a measured worst-case free-stack value.
The ELF does not account for the maximum call chain, local variables that are
live simultaneously, interrupt nesting, or compiler-generated spills. Treat a
successful link as necessary but insufficient: release validation must also
analyse the largest foreground and interrupt call paths and, preferably,
measure the stack high-water mark on hardware using a fill-pattern/canary test.

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

