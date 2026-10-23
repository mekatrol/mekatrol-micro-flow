# Project Structure

## Purpose

Mekatrol Micro Flow has two production-code areas: the portable library and
complete board integrations. Both live under `src/` so source ownership is
visible in one place and a board build does not have to assemble parallel
`ports`, `soc`, `boards`, and `examples` trees.

Firmware is composed from separate physical-board, SoC, and optional
family-common layers. The dependency direction is:

```text
src/boards/<family>/<board>  --->  src/boards/<family>/<soc>
             |                              |
             +----------> src/boards/<family>/common
             |                              |
             +------------------------------+
                            v
                         src/lib
```

The library never depends on board code. SoC and family-common code never
contain physical-board pin assignments. A concrete board folder owns the entry
point and physical resource mapping. Its SoC folder owns chip-specific startup,
register support, and linker layouts. Files can move into a family `common/`
folder only when two implemented SoCs genuinely share them.

## Repository layout

```text
AGENTS.md
PLANS.md
PROJECT_STRUCTURE.md
PROJECT_SETUP.md
README.md
BUILD.md
LICENSE
CMakeLists.txt
CMakePresets.json
cmake/
  toolchains/
  boards/
src/
  lib/
    include/microflow/
    core/
    blocks/
    runtime/
    host/
  boards/
    <family>/
      common/           Optional cross-SoC implementation
      <soc>/            Chip startup, registers, and linker layout
      <specific-board>/ Entry point and physical resource mapping
tests/
  unit/
  integration/
  hardware/
third_party/
docs/
tools/
```

Folders are added with their first real source or documentation file. A board
folder containing only a README is a planned board, not an implemented target.

## Root and build files

`PLANS.md` is the implementation roadmap and status record. This document is
the authority for file placement and dependencies. `PROJECT_SETUP.md` documents
tool installation, while `BUILD.md` documents normal build and test workflows.

The root `CMakeLists.txt` composes library, test, or selected firmware targets.
`CMakePresets.json` supplies repeatable host and explicit-board configurations.
`cmake/toolchains/` controls compiler selection. `cmake/boards/` contains build
composition only; it does not contain production C or assembly code or repeat
board hardware facts.

## `src/lib/`

`src/lib/` is the embeddable Micro Flow library. It must remain portable ISO C,
must not include vendor SDK headers, and must not depend on anything under
`src/boards/`.

- `include/microflow/` contains installed public headers. These headers form
  the supported API and cannot expose board, chip, compiler, or SDK types.
- `core/` contains graph validation, execution, values, state, limits, and
  lifecycle behavior.
- `blocks/` contains portable function blocks. Hardware blocks consume logical
  capabilities rather than pins or peripheral registers.
- `runtime/` contains portable scheduling, capability registration,
  diagnostics, and standalone-runtime contracts. Internal headers remain here.
- `host/` contains deterministic host-only support for simulation and tests.

An application embedding only the core is not required to link the runtime or
host support.

## `src/boards/`

Board support is grouped first by vendor or closely related microcontroller
family, then separated into SoC support and exact physical boards. The
maintained family names are `rpi`, `esp32`, `ch32`, and `stm32`.

Each family may contain SoC folders such as `rp2040`, `rp2350`, `esp32-s3`, or
`esp32-c6`. Startup, linker layouts, register definitions, and chip drivers
belong to these folders so multiple boards using the same SoC can reuse them.
These layers may link pinned native SDK components and maintained middleware,
including vendor USB stacks, provided the dependency remains private to board
firmware and is adapted to project-owned interfaces. Vendor headers and types
must not cross into `src/lib` or public Micro Flow APIs.

Each family may also contain `common/` for code proven to be shared by multiple
implemented SoCs in that family. A common folder must not become a second
abstraction framework: keep register differences in the SoC folder and
physical-board facts in the specific board folder. Do not create placeholder
`common/` or SoC folders before they own real shared implementation or required
SoC documentation.

A typical SoC folder may contain:

```text
src/boards/<family>/<soc>/
  startup.S           Reset and architecture startup for the SoC
  <soc>.c/.h          Register-level chip support
  <soc>.ld            Linker and memory layout
  README.md           SoC implementation status and constraints
```

A specific board folder may contain:

```text
src/boards/<family>/<board>/
  main.c              Standalone demonstration/runtime entry point
  board.c             Logical resources and physical pin mapping
  README.md           Hardware, flashing, debugging, and status
```

The entry point demonstrates library integration, so there is no separate
top-level `examples/` tree. Reusable flow behavior belongs in the library;
board bring-up behavior belongs with its board.

The initial board inventory is:

| Family | Board folder | Status |
|---|---|---|
| `rpi` | `waveshare-rp2040-zero` | Partial firmware target |
| `rpi` | `raspberry-pi-pico` | Partial firmware target |
| `rpi` | `raspberry-pi-pico-2` | Experimental Arm and RISC-V compile targets |
| `esp32` | `waveshare-esp32-s3-zero` | Partial firmware target |
| `esp32` | `waveshare-esp32-c6-zero` | Partial firmware target |
| `esp32` | `espressif-esp32-c3-devkit` | Planned target |
| `ch32` | `ch32v003` | Planned WCH vendor development board |
| `ch32` | `ch32v203` | Planned WCH vendor development board |
| `stm32` | `stm32f407-discovery` | Planned STM32F407 Discovery kit |

Board names identify physical products rather than only the fitted SoC. The
requested WCH vendor boards retain the concise `ch32v003` and `ch32v203` names.

## Tests and supporting material

`tests/` is never a production-code dependency. Unit and integration tests run
on the host; hardware tests contain reusable conformance firmware, fixtures,
and procedures. A successful firmware compile is not a hardware test.

`third_party/` holds intentionally imported boot artifacts or definitions,
each with pinned provenance, licence, modifications, and update instructions.
`docs/` contains detailed design or provenance material. `tools/` contains
host-side build/image utilities, not target production code.

Generated build directories and compiler outputs must not be committed.
