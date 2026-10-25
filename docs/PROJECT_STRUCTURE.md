# Project Structure

## Dependency direction

Mekatrol Micro Flow has a portable library and two Raspberry Pi board
integration directories. The RP2040 integration has compile-time mappings for
the Raspberry Pi Pico and Waveshare RP2040-Zero:

```text
src/boards/rpi/raspberry-pi-pico   ---> src/boards/rpi/rp2040
src/boards/rpi/raspberry-pi-pico-2 ---> src/boards/rpi/rp2350
                         |
                         v
                      src/lib
```

The library never depends on board or SoC code. SoC code never contains
physical-board pin assignments.

## Repository layout

```text
src/
  lib/
    include/microflow/       Public portable API
    core/                    Flow engine
    blocks/                  Portable function blocks
    runtime/                 Portable runtime contracts
    host/                    Host simulation and test support
  boards/rpi/
    rp2040/                  Pico SDK-backed RP2040 and USB CDC adapters
    rp2350/                  RP2350 Arm/RISC-V startup and linker layouts
    raspberry-pi-pico/       Pico/RP2040-Zero entry point and resource mappings
    raspberry-pi-pico-2/     Pico 2 entry point and resource mapping
cmake/
  boards/                    Firmware target composition
  toolchains/                Arm and RISC-V compiler selection
tests/
  unit/                      Host regression tests
tools/                       Build, clean, and image utilities
```

## Ownership rules

The physical board directories own entry points, pins, exposed logical
resources, board limits, and board documentation. The SoC directories own
startup, interrupt dispatch, register support, chip drivers, and linker layouts.

Portable flow storage, console parsing, and upload framing belong in `src/lib`.
Board applications inject hardware operations into those modules.
Physical GPIO mappings and firmware presentation remain in the board directory;
SDK calls and peripheral drivers remain in the SoC directory. FreeRTOS is a
firmware integration detail and must not leak into `src/lib`. RP2040 and RP2350
differences must remain explicit.

`cmake/boards` contains build composition, not production C or assembly.
Generated build directories and compiler outputs are not source.
