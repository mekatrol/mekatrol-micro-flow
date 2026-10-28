# C firmware build, test, and flash

## Prerequisites

Install CMake 3.25+, Ninja, a host C11 compiler, and the exact xPack GNU
RISC-V Embedded GCC 14.3.0-1 release described in
[PROJECT_SETUP.md](PROJECT_SETUP.md). Its `bin` directory must be on `PATH`.
Normal builds make no network requests.

The supported presets are `host-debug`, `host-release`, `ch32v003-debug`, and
`ch32v003-release`. All generated files stay below `build/<preset>/`.

## Clean, test, and build

From `output-expander/`:

```sh
tools/clean.sh
tools/test.sh host-debug
tools/build.sh ch32v003-debug --clean
tools/build.sh ch32v003-release --clean
```

PowerShell exposes the same operations with `tools/clean.ps1`,
`tools/test.ps1`, and `tools/build.ps1`. The firmware build emits
`output-expander.elf`, `.bin`, `.map`, and `.dis`, prints memory use, and fails
if Flash exceeds 16 KiB or static SRAM exceeds 2 KiB.

## Smoke-image behavior

Startup selects the 48 MHz HSI/PLL clock and performs these operations before
the heartbeat:

1. Latch PC0, PC3, PC4, PC5, PD0, PD2, PD3, and PD4 HIGH (active-low OFF).
2. Configure those pins as push-pull outputs.
3. Hold PC6 LOW so tied RS-485 DE and `/RE` remain receive-safe.
4. Toggle PC0 every 500 ms, producing a 1 Hz full on/off cycle.

The test LED/load must be suited to the board output stage and current limited.
Do not connect an LED directly across an unknown output stage without checking
the schematic and board revision.

## Flash

Connect WCH-LinkE SWIO to PD1 and GND to GND. Power the board normally; leave
the WCH-LinkE 3V3 pin disconnected when the board is independently powered.
Then run:

```sh
tools/flash.sh ch32v003-release
```

The wrapper builds first, checks the exact ELF exists, and explicitly invokes
`wlink -v flash --chip CH32V003 --speed low`. Declaring the target avoids
auto-detection ambiguity and the conservative 400 kHz SWIO rate improves attach
margin. A normal build never flashes. If no probe is attached or USB
permissions are missing, flashing fails without claiming success.

`WCH-Link underlying protocol error: 0x55` means the probe failed to attach to
the target; it is reported before the ELF is programmed. If it persists at low
speed, power-cycle the target and probe, confirm common ground and PD1/SWIO,
and hold NRST low while starting the command, then release it when attachment
begins. Do not connect the probe's 3V3 output when the board is independently
powered.

## Rust recovery

The known reference implementation remains independently buildable and its
clean operation cannot touch this project's `build/` directory:

```sh
cd ../output-expander-rust
cargo clean
cargo firmware
cargo flash
```

`cargo flash` invokes `wlink -v flash` for the Rust release ELF. Use this to
restore the reference firmware after smoke-image validation.
