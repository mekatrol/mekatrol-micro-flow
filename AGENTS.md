# Mekatrol Micro Flow Repository Instructions

## Purpose

Mekatrol Micro Flow is a small, portable C library for deterministic,
flow-based automation on resource-constrained microcontrollers. The flow engine
is the product. Board support exists to run, test, and demonstrate the engine;
it must not couple the core to a chip vendor.

The project supports both library integration into existing firmware and a
minimal standalone runtime. The maintained roadmap is in `PLANS.md`; consult it
when selecting or implementing planned work.

Initial targets are RP2040, RP2350, ESP32-S3, ESP32-C3, ESP32-C6, CH32V003, and
a host simulator/test build.

## Design rules

- Keep the flow engine portable ISO C. Target C11 unless a supported compiler
  requires a documented subset.
- Do not expose vendor SDK types or headers through public Micro Flow APIs.
- Do not call vendor HAL or SDK runtime APIs from the core, portable blocks,
  common runtime, or common hardware interfaces.
- Prefer static allocation. Dynamic allocation must be optional, bounded, and
  isolated behind an interface.
- Do not assume an OS, threads, filesystem, networking, standard output, or wall
  clock.
- Keep execution time and memory use measurable and bounded.
- Treat malformed flows and unavailable capabilities as errors, never undefined
  behaviour.
- Hardware function blocks use logical resources. Boards map those resources to
  pins and peripherals.
- Validate all required capabilities before a flow begins execution.

## Hardware boundaries

`PROJECT_STRUCTURE.md` is the authoritative source for repository layout,
dependency direction, folder responsibilities, and placement of new files.
Reference it rather than repeating its structural descriptions in other README
or guidance files.

A board combines an architecture port with an SoC implementation. Do not copy
an SoC driver tree to add a board, and do not move board-specific conveniences
into the portable engine.

Begin the hardware capability interface with monotonic time, deadline waiting,
digital I/O, diagnostic byte output, panic/reset, and watchdog servicing. Add
ADC, PWM, buses, storage, and communications only for a tested block or runtime
requirement. Keep interrupt handlers inside the relevant SoC layer and transfer
events through bounded structures with documented concurrency rules.

## Vendor dependency policy

Direct register-level hardware control is preferred over vendor abstraction
libraries. The project may use vendor documentation, compilers, linkers,
debuggers, host-side flashing/image tools, raw register definitions, CMSIS-Core,
compiler runtime helpers, and narrowly scoped boot artifacts.

Normal standalone targets must not link vendor HAL or SDK runtime libraries.
Any exception must be documented per target, confined to the boot boundary, and
removable without changes to `microflow-core`.

Permitted initial boot boundaries are:

- RP2040: official second-stage flash boot implementation
- RP2350: official boot metadata and narrowly required boot code
- ESP32 family: Espressif ROM loader, second-stage bootloader, image utilities,
  and flashing tools, but no ESP-IDF runtime services in the application
- CH32V003: WCH compiler and debug tools, but project-owned runtime drivers

Record the source, pinned version, licence, modifications, and update procedure
for every imported boot artifact or register definition.

## Build system

Use CMake with Ninja as the preferred local and CI backend.

- Put compiler selection in CMake toolchain files.
- Represent chips and boards with targets and presets, not scattered global
  preprocessor conditionals.
- Use target-scoped includes, definitions, compile options, and link options.
- Require an explicit board preset for firmware builds.
- Produce ELF and map files plus the appropriate flashable image.
- Report flash and RAM consumption; fail when configured board limits are
  exceeded.
- Keep flashing and debugging explicit. A normal build must not modify attached
  hardware.
- Pin external tools and boot components.
- Maintain repeatable host debug/release and board presets in
  `CMakePresets.json`.

## Verification

Every portable bug fix requires a host regression test. Each driver requires a
hardware test or a documented reason automated testing is impractical.

Compile with strong warnings and treat warnings as errors in CI. Use host
sanitizers where supported. Common hardware conformance covers boot, runtime
initialization, timer progression and wraparound, digital I/O, flow scheduling,
overrun detection, watchdog recovery, reset cause, capability rejection, panic
diagnostics, and repeated cold/warm resets.

A target is supported only when it has a pinned toolchain, reproducible preset,
source-controlled startup and linker layout, flashable image, flash/debug
instructions, required basic capabilities, passing conformance tests, size
report, dependency provenance, and a maintained board configuration. Otherwise
label it experimental or partial.

Before changing public APIs, scheduling, value representation, state,
serialization, or capabilities:

1. Check the effect on every initial target, especially CH32V003 memory limits.
2. Add or update host tests.
3. Measure memory and execution impact.
4. Run affected hardware conformance tests.
5. Update documentation and dependency records.

Keep changes small enough that the repository remains buildable and testable.
Do not commit generated build directories or compiler outputs. Commit a binary
artifact only when it is an intentionally pinned boot dependency with documented
provenance.
