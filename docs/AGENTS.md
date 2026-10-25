# Mekatrol Micro Flow Repository Instructions

## Purpose

Mekatrol Micro Flow is a small, portable C11 library for deterministic,
flow-based automation on resource-constrained microcontrollers. The maintained
hardware targets are Raspberry Pi Pico (RP2040) and Raspberry Pi Pico 2
(RP2350), with both Arm and RISC-V Pico 2 builds. Host builds provide the
portable reference and regression tests.

The flow engine is the product. Board support exists to run, test, and
demonstrate it and must not couple portable code to Raspberry Pi silicon.

## Design rules

- Keep `src/lib` portable ISO C11 and independent of Pico-specific headers,
  SDK types, registers, and runtime APIs.
- Prefer static allocation. Any dynamic allocation must be optional, bounded,
  and isolated behind an interface.
- Do not assume an OS, threads, filesystem, networking, standard output, or a
  wall clock.
- Keep execution time and memory use measurable and bounded.
- Treat malformed flows and unavailable capabilities as errors.
- Hardware blocks use logical resources; board code maps them to pins and
  peripherals.
- Validate all required capabilities before a flow begins execution.

## Hardware boundaries

`PROJECT_STRUCTURE.md` is authoritative for repository layout and dependency
direction. Pico and Pico 2 board directories own physical resource mappings and
entry points. RP2040 and RP2350 directories own SoC startup, linker layouts,
register support, and chip drivers. Do not duplicate an SoC driver tree in a
board directory.

Interrupt handlers stay in the relevant SoC layer and transfer work through
bounded structures with documented concurrency and overflow rules.

## Vendor dependencies

The portable library remains independent of vendor dependencies. RP2040 and
RP2350 integrations may use pinned Raspberry Pi Pico SDK components and TinyUSB
inside the SoC or board boundary when that improves reliability. Adapt such
components to project-owned interfaces and prevent SDK types, allocation
policy, and lifecycle assumptions from leaking into `src/lib`.

Record source, version, licence, enabled components, local modifications, and
update procedure for every linked dependency or imported artifact.

## Build and verification

Use CMake presets with Ninja. Keep host builds working on Windows, macOS, and
Linux. Toolchain selection belongs in CMake toolchain files; board selection
belongs in explicit presets and targets.

Firmware builds must produce ELF and map files plus the appropriate flashable
image when boot support exists. Report flash and RAM use and keep flashing an
explicit operation.

Every portable bug fix requires a host regression test. Each driver requires a
hardware test or a documented reason automation is impractical. Compile with
strong warnings and warnings-as-errors in CI, and use host sanitizers where
supported.

A target is supported only when it has a pinned toolchain, reproducible preset,
startup and linker support, a flashable image, flash/debug instructions, basic
capabilities, passing conformance tests, size evidence, dependency provenance,
and a maintained board configuration. Otherwise label it partial or
experimental.

Before changing public APIs, scheduling, value representation, state,
serialization, or capabilities, check both RP2040 and RP2350, add or update host
tests, measure memory and execution impact, and update documentation.

Do not commit generated build directories or compiler outputs.
