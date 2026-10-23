# Mekatrol Micro Flow Development Plan

This roadmap describes intended implementation order. It is not evidence that a
phase or target is complete. Update status and verification evidence as work is
performed.

## Intended repository structure

See `PROJECT_STRUCTURE.md` for the authoritative repository layout, dependency
direction, and folder responsibilities. This plan records implementation order
and does not duplicate that structural reference.

Expected initial CMake presets are:

```text
host-debug
host-release
waveshare-rp2040-zero-debug
raspberry-pi-pico-debug
raspberry-pi-pico-2-arm-debug
raspberry-pi-pico-2-riscv-debug
esp32-s3-debug
esp32-c3-debug
esp32-c6-debug
ch32v003-debug
```

Both RP2350 architecture presets exist as compile/link smoke tests. Promote
them to firmware targets only after boot metadata, packaging, board drivers,
and conformance are implemented.

## Target strategy

### Host

Develop the host port first and retain it as the functional reference. Provide a
deterministic virtual clock, simulated I/O, fault injection, execution tracing,
unit tests, integration tests, and sanitizer builds.

### RP2040

Use Raspberry Pi Pico as the first physical target. Implement project-owned
reset, clocks, timer, GPIO, polling UART, watchdog, and interrupt dispatch at
register level. Reuse only the small official flash second-stage boot code and
image tooling needed for reliable external-flash boot.

Produce ELF, BIN, and UF2 artifacts. Support SWD debugging and BOOTSEL
deployment.

### RP2350

Use Pico 2 and initially target Arm Cortex-M33. Treat RP2350 as a separate SoC,
not RP2040 conditionals. Support required boot metadata and image rules. Begin
single-core and add multicore operation only after the single-core runtime is
stable and measured.

Add a separate RISC-V architecture build later. Portable flows must behave
identically on both architectures unless a documented capability differs.

### ESP32-S3

Use ESP32-S3 for the initial Xtensa port. Use Espressif's ROM loader,
second-stage bootloader, image format, and host flashing tools. Own application
startup after bootloader handoff and implement timer, GPIO, UART, interrupt, and
watchdog integration at register level.

Start single-core with flash and caches in the documented bootloader-provided
state. Defer the second core, USB, radio, external RAM, and advanced power
management.

### ESP32-C3

Use ESP32-C3 for the first Espressif RISC-V port. Share architecture code with
ESP32-C6 only where the documented privileged architecture and interrupt model
match. Keep register maps and boot configuration SoC-specific. Use the same
bootloader/tool boundary as ESP32-S3.

### ESP32-C6

Add ESP32-C6 after C3. Reuse proven Espressif image and RISC-V infrastructure,
while implementing C6-specific clocks, interrupts, and peripherals separately.
Defer wireless subsystems until basic runtime conformance passes.

### CH32V003

Use CH32V003 as the small-memory target. Treat its QingKe core and interrupt
behaviour as a dedicated architecture variant rather than interchangeable with
Espressif RISC-V implementations.

Implement startup, linker layout, clock, monotonic timer, GPIO, polling UART,
watchdog, and single-wire debug/flashing. This target defines the initial
minimum-footprint configuration.

Use the WCH vendor CH32V003 development board as the physical target, stored as
`src/boards/ch32/ch32v003`.

### CH32V203

Add the WCH vendor CH32V203 development board after the CH32V003 port has made
the shared QingKe and peripheral assumptions explicit. Share code through
`src/boards/ch32/common` only where both implementations are verified to match.

### STM32F407

Use the STM32F407 Discovery kit for the first STM32 target. Keep its Cortex-M
startup, register-level drivers, linker layout, and physical resource mapping
together in `src/boards/stm32/stm32f407-discovery`; extract family-common code
only when another STM32 board needs it.

## Phase 1: portable core and build foundation

Status: Not started

1. Create CMake targets for the core, portable blocks, runtime, and host tests.
2. Add host debug and release presets using Ninja.
3. Define fixed-width values, error handling, graph limits, and allocation
   policy.
4. Implement validation before execution.
5. Implement deterministic host time and simulated digital I/O.
6. Test graph validation, propagation, stateful blocks, timers, malformed input,
   and resource exhaustion.
7. Run warnings-as-errors and supported host sanitizers.

Exit criterion: the host executes a useful flow deterministically without
target-specific source in the core or portable blocks.

## Phase 2: bootstrap contract

Status: Not started

1. Define internal architecture, SoC, board, and capability APIs.
2. Define the state guaranteed at `microflow_device_main()` entry.
3. Provide minimal freestanding initialization for `.data`, `.bss`, panic, and
   required compiler-runtime integration.
4. Define consistent linker symbols.
5. Add map-file inspection and size reporting.
6. Define hardware conformance tests reusable across boards.

Exit criterion: adding a board has a written checklist and requires no portable
engine changes.

## Phase 3: RP2040 reference hardware port

Status: Not started

1. Boot Pico into `microflow_device_main()`.
2. Bring up polling diagnostics, monotonic time, GPIO, reset, and watchdog.
3. Execute a timer-to-digital-output flow.
4. Validate interrupt timing and timer wraparound.
5. Document repeatable UF2 and SWD deployment.
6. Record code size, RAM use, cycle time, and jitter for the reference flow.

Exit criterion: Pico passes hardware conformance and an extended runtime test
without drift, unexpected allocation, or watchdog failure.

## Phase 4: architecture and resource validation

Status: Not started

1. Port CH32V003 to establish the minimum configuration and expose hidden memory
   assumptions.
2. Port ESP32-C3 to validate the general RISC-V structure independently of
   QingKe.
3. Compare results from identical golden flows on host and all three devices.

Exit criterion: host, RP2040, CH32V003, and ESP32-C3 produce equivalent
observable results for portable flows.

## Phase 5: remaining initial targets

Status: Not started

1. Add RP2350 Arm support.
2. Add ESP32-S3 and the Xtensa port.
3. Add ESP32-C6 using proven Espressif RISC-V infrastructure.
4. Add RP2350 RISC-V after the initial matrix is stable.
5. Run conformance and publish memory/timing results for every target.
6. Add the CH32V203 vendor board and STM32F407 Discovery kit after the initial
   target matrix is stable.

Exit criterion: every initial target builds reproducibly, boots from a clean
device using documented tools, passes conformance, and has a maintained board.

## Phase 6: integration features

Status: Not started

After the target matrix is stable, add persistent flow storage, robust flow
updates, richer diagnostics, ADC/PWM capabilities, and communications. Add each
behind a capability and retain builds that exclude it completely.

Wireless support is not part of processor bring-up. If introduced, keep it
behind explicit runtime services and never make a networking stack a dependency
of the flow engine.

Exit criterion: each integration feature can be included or excluded at build
time, is covered by host and applicable hardware tests, and does not change
portable flow semantics.

## Plan maintenance

For each completed phase, record the relevant build and test commands, observed
results, memory measurements, unresolved limitations, and target status. Do not
mark a phase complete based only on a successful compile.
