# Project Structure

## Purpose of the structure

Mekatrol Micro Flow separates the portable flow engine from processor,
microcontroller, and physical-board details. This allows the same flow and
function-block implementation to run in a host test process, inside existing
firmware, or as a standalone bare-metal image.

The structure is designed to enforce these distinctions:

- **Portable behaviour** belongs in the public library, core, blocks, or common
  runtime.
- **CPU architecture behaviour** belongs in an architecture port.
- **Microcontroller peripheral behaviour** belongs in an SoC implementation.
- **Physical hardware configuration** belongs in a board definition.
- **Build composition** belongs in CMake targets, toolchain files, and presets.
- **Verification** remains separate from production source.

The intended dependency direction is:

```text
Application or example
        │
        ▼
Public Micro Flow API
        │
        ├──────────────► Portable function blocks
        │
        ▼
Core engine ◄────────── Common runtime
                            │
                            ▼
                    Hardware capabilities
                            │
                            ▼
Board configuration ──► SoC drivers ──► Architecture port
```

Dependencies must not point upward. In particular, the core must not include
headers from `ports/`, `soc/`, or `boards/`, and an SoC driver must not contain
Pico, DevKit, or other physical-board pin assignments.

## Intended top-level layout

```text
AGENTS.md
PLANS.md
PROJECT_STRUCTURE.md
README.md
LICENSE
CMakeLists.txt
CMakePresets.json
cmake/
  toolchains/
  boards/
include/
  microflow/
src/
  core/
  blocks/
  runtime/
ports/
  host/
  arm-cortex-m/
  riscv/
  xtensa/
soc/
  rp2040/
  rp2350/
  esp32-s3/
  esp32-c3/
  esp32-c6/
  ch32v003/
boards/
  pico/
  pico2/
  esp32-s3-devkit/
  esp32-c3-devkit/
  esp32-c6-devkit/
  ch32v003-dev-board/
examples/
tests/
  unit/
  integration/
  hardware/
third_party/
docs/
```

This is the intended mature layout. Folders should be added when their first
real file is needed rather than created empty.

## Root files

### `AGENTS.md`

Contains durable repository instructions and working agreements. It defines
architectural boundaries, dependency policy, build conventions, and minimum
verification expectations.

It should remain concise. Detailed roadmaps, design explanations, and long
procedures should be referenced from other documents instead of copied into
this file.

### `PLANS.md`

Contains the implementation roadmap, target order, phase status, and exit
criteria. It describes intended future work and must not be treated as evidence
that a feature has been implemented or tested.

### `PROJECT_STRUCTURE.md`

Describes ownership of files and dependencies between layers. Update it when a
new top-level subsystem or a materially different source category is added.
This is the single source of truth for repository structure. Other README and
guidance files should link here and document only their local usage, setup, or
constraints instead of repeating the general folder descriptions.

### `README.md`

Introduces the project to users. It should describe what the library does,
supported and experimental targets, a small usage example, basic build entry
points, and links to detailed documentation.

The README is not the place for internal architecture rules or a complete port
implementation procedure.

### `LICENSE`

Contains the project licence. Imported components retain their own licence and
provenance files under `third_party/`.

### `CMakeLists.txt`

The root build entry point. It normally:

- Declares the project and supported languages
- Defines global build options
- Adds the portable library targets
- Selects host tests or a firmware composition
- Includes subdirectories containing real targets
- Enables testing when requested
- Avoids target-specific compiler and linker flags at global scope

Most implementation details should live with the target that owns them rather
than accumulate in the root file.

### `CMakePresets.json`

Defines repeatable named configurations for host and board builds. A preset
selects a toolchain file, board, build type, generator, and output directory.

Typical presets include:

```text
host-debug
host-release
pico-rp2040-debug
pico2-rp2350-arm-debug
esp32-s3-debug
esp32-c3-debug
esp32-c6-debug
ch32v003-debug
```

Presets must not contain developer-specific absolute paths or credentials.

## `cmake/`

Contains reusable build-system modules. It must not contain C production code.

### `cmake/toolchains/`

Describes how CMake invokes a compiler for a CPU architecture or toolchain.
Typical contents include:

- `arm-none-eabi-gcc.cmake`
- `riscv-none-elf-gcc.cmake`
- `xtensa-esp32s3-elf-gcc.cmake`
- `ch32v003-gcc.cmake`
- Optional common helpers shared by related compiler files

A toolchain file normally sets:

- C, assembly, linker, `objcopy`, and `size` programs
- Bare-metal system identity and try-compile behaviour
- Architecture ABI defaults that apply to the selected compiler
- Search-path rules that prevent accidental host-library linkage

It should not select LED pins, flash size, an example program, or a particular
physical board.

### `cmake/boards/`

Contains CMake composition modules for supported boards. These modules connect
the appropriate architecture, SoC, board sources, linker layout, compiler
options, and post-link image generation.

For example, a Pico build module may select:

- The Arm Cortex-M architecture target
- The RP2040 SoC target
- The `boards/pico/` configuration target
- The RP2040 boot-stage object
- A linker script configured for the board's flash size
- BIN and UF2 post-link outputs

This folder contains build logic; `boards/pico/` contains hardware facts and
source code. Do not duplicate board constants between the two. Prefer exposing
the hardware facts from the board target and consuming them in the CMake module.

Common CMake helpers that do not belong to a particular board may be placed
directly under `cmake/`, such as size checking, linker-map generation, and image
conversion functions.

## `include/microflow/`

Contains installed public headers for users embedding the library. Everything
here forms part of the supported public API and must remain independent of any
vendor SDK.

Typical public headers may include:

```text
include/microflow/
  microflow.h          Main library entry points
  config.h             Compile-time limits and configuration types
  error.h              Public error codes
  value.h              Values and type identifiers
  flow.h               Flow description and lifecycle
  block.h              Function-block contracts
  capability.h         Hardware capability identifiers and handles
  version.h            Library version information
```

Public headers should:

- Use fixed-width integer types where width matters
- Avoid exposing internal structure layouts unnecessarily
- Avoid compiler-, board-, and SDK-specific types
- Be usable by both hosted and freestanding applications
- Document ownership, lifetime, valid ranges, and error behaviour

Headers used only inside the implementation belong under the relevant source
folder, not in the installed include tree.

## `src/`

Contains portable production code. Code under `src/` must compile for the host
and every supported embedded compiler without target-specific SDK headers.

### `src/core/`

Owns the flow engine's fundamental behaviour. Typical contents include:

- Flow graph representation
- Flow validation
- Node and connection lookup
- Execution ordering
- Input propagation and output publication
- Typed value handling
- Block-instance state storage
- Resource-limit checks
- Engine lifecycle and error reporting

Representative files might be:

```text
src/core/
  engine.c
  flow.c
  graph.c
  validate.c
  value.c
  state.c
  internal.h
```

The core must not know which GPIO controller, timer, board, compiler, or
operating environment is in use. It may depend on public types and small
portable internal utilities.

### `src/blocks/`

Contains reusable function-block implementations. Organize blocks by behaviour
when the collection grows:

```text
src/blocks/
  logic/
  comparison/
  math/
  timing/
  state/
  conversion/
  hardware/
```

Pure blocks such as AND, comparison, addition, limit, and selection operate only
on their declared inputs, outputs, configuration, and state. Timing blocks use
the runtime's monotonic time contract rather than reading a device timer.

Hardware-facing blocks request a generic capability such as digital input or
PWM output. They must not include an RP2040, ESP32, or CH32 header and must not
embed physical pin numbers in a serialized flow.

### `src/runtime/`

Contains portable services used by the standalone firmware and optionally by
host applications. Typical responsibilities include:

- Runtime initialization and shutdown
- Cyclic or deadline-driven scheduling
- Execution-overrun detection
- Capability registration and lookup
- Flow activation and replacement
- Diagnostic event formatting
- Panic-policy coordination
- Optional persistence interfaces

The runtime defines *when* a flow executes and which capabilities are exposed.
It does not implement device registers. An application embedding only the core
may choose not to link this target.

## `ports/`

Contains CPU-architecture and execution-environment support. A port deals with
instruction-set and ABI concerns that are independent of a specific peripheral
set or development board.

Typical port responsibilities include:

- Reset-entry assembly where appropriate
- Exception entry and return conventions
- Interrupt enable/disable primitives
- Memory barriers and low-power wait instructions
- Atomic or critical-section primitives
- Stack initialization rules
- Compiler ABI helpers not supplied elsewhere
- Architecture-specific context needed by panic diagnostics

### `ports/host/`

Implements a hosted test environment using normal process facilities. It may
provide:

- Deterministic virtual time
- Simulated GPIO and other capabilities
- Fault injection
- Execution traces
- Test-only synchronization

Host conveniences must not leak into portable public APIs.

### `ports/arm-cortex-m/`

Contains code shared by compatible Arm Cortex-M targets, such as vector-table
support, exception helpers, interrupt masking, barriers, and wait instructions.

RP2040 and RP2350 can share code only where their core architecture and required
behaviour truly match. SoC startup sequences, clocks, and peripheral registers
remain outside this folder.

### `ports/riscv/`

Contains reusable RISC-V architecture support. Variants should be explicit
where privilege, interrupt, ABI, or vendor-core behaviour differs.

ESP32-C3 and ESP32-C6 may share common code. CH32V003's QingKe-specific startup
and interrupt behaviour should live in a clearly named subfolder or source set,
not be hidden behind assumptions that all RISC-V targets behave alike. Future
RP2350 RISC-V support must likewise select only genuinely compatible pieces.

### `ports/xtensa/`

Contains Xtensa architecture support required by ESP32-S3, including exception
entry, interrupt-level handling, barriers, and ABI-specific startup work. It
must not contain ESP32-S3 pin assignments or general peripheral drivers.

## `soc/`

Contains microcontroller-specific support. SoC code understands the complete
chip but not the physical board on which it is mounted.

Each SoC folder typically contains:

```text
soc/<soc>/
  CMakeLists.txt
  include/
    microflow/soc/<soc>/
  startup/
  registers/
  clock.c
  interrupt.c
  gpio.c
  timer.c
  uart.c
  watchdog.c
  reset.c
  memory.ld.in
```

Only create drivers that are required by a tested capability or runtime
feature. DMA, ADC, PWM, flash programming, USB, radio, and other drivers should
not appear merely to reproduce a vendor SDK.

### `soc/rp2040/`

Owns RP2040 clocks, resets, GPIO, timer, UART, watchdog, interrupt routing,
multicore control when supported, and the interface to its external-flash boot
stage. It does not decide which Pico pin carries an LED.

### `soc/rp2350/`

Owns RP2350-specific boot metadata integration, clocks, power/reset control,
interrupts, peripherals, and the differences between its Arm and RISC-V boot
modes. Avoid implementing it as a collection of conditionals inside RP2040.

### `soc/esp32-s3/`

Owns ESP32-S3 register maps, application handoff after the Espressif bootloader,
clock and reset control, interrupt matrix integration, timer, GPIO, UART, and
watchdog. Xtensa exception mechanics remain in `ports/xtensa/`.

### `soc/esp32-c3/`

Owns ESP32-C3-specific peripherals, boot handoff, clocks, interrupt controller,
timer, GPIO, UART, and watchdog. Compatible architecture helpers may come from
`ports/riscv/`, but C3 register definitions stay here.

### `soc/esp32-c6/`

Owns the corresponding ESP32-C6 implementation. It may share carefully factored
Espressif common code with C3, but only after the common behaviour is verified.
Do not force unlike register maps into a single driver through extensive
preprocessor branching.

### `soc/ch32v003/`

Owns CH32V003 clock setup, interrupt controller, SysTick or selected monotonic
timer, GPIO, UART, watchdog, reset causes, flash layout, and WCH-specific debug
considerations. QingKe architecture mechanics remain distinct within the RISC-V
port.

## `boards/`

Contains definitions for physical products and development boards. A board
selects one SoC and records facts imposed by its schematic and fitted parts.

A typical board folder contains:

```text
boards/<board>/
  CMakeLists.txt       Board target and source list
  board.c              Early board initialization and resource mapping
  board.h              Board-local constants
  board_config.h.in    Generated compile-time configuration, if required
  memory.ld.in         Board override only when memory is board-dependent
  README.md            Wiring, flashing, debugging, and limitations
```

Typical board-owned information includes:

- Selected SoC and package
- External crystal or oscillator frequency
- External flash type, bus mode, and capacity
- External RAM, when fitted
- LED and button pins, including polarity
- Default diagnostic UART pins
- USB connector and boot/reset circuit details
- Debug connector and probe settings
- Pins reserved by on-board hardware
- Logical Micro Flow resource-to-pin mappings
- Board-specific power or peripheral enable signals

Memory facts should live at the lowest correct layer. On-chip RAM is an SoC
fact. The fitted external flash capacity is usually a board fact. A linker script
may therefore be an SoC template configured by board parameters.

### `boards/pico/`

Describes the Raspberry Pi Pico: RP2040 selection, fitted flash, oscillator,
BOOTSEL behaviour, LED pin, default UART routing, reserved pins, and SWD/UF2
instructions.

### `boards/pico2/`

Describes Pico 2 and selects RP2350. It records the fitted memory and board pin
assignments and exposes separate Arm and, later, RISC-V build compositions where
necessary.

### `boards/esp32-s3-devkit/`

Describes the exact supported ESP32-S3 development-kit model and revision,
including flash/PSRAM population, USB/UART arrangement, LED, boot/reset buttons,
and safe exposed pins. Rename this folder to the exact board designation if
multiple S3 DevKit variants would otherwise be ambiguous.

### `boards/esp32-c3-devkit/`

Provides the equivalent physical configuration for the selected ESP32-C3 board.
Do not assume its LED, USB, or flash configuration matches other Espressif
development kits.

### `boards/esp32-c6-devkit/`

Provides the physical configuration for the selected ESP32-C6 board, including
any USB serial/JTAG wiring and pins reserved for fitted radio or flash hardware.

### `boards/ch32v003-dev-board/`

Is a temporary name until an exact CH32V003 board and package are selected. Once
selected, rename it to an unambiguous board name and document the package pin
count, clock source, LED, programming connector, and available I/O.

## `examples/`

Contains small user-facing programs demonstrating supported integration paths.
Examples are documentation that must continue to build.

Typical examples include:

```text
examples/
  minimal-library/
  blink-flow/
  digital-interlock/
  timer-counter/
  custom-block/
  standalone-runtime/
```

Each example should focus on one concept, use public APIs, include a short
README, and avoid becoming an alternative application framework. A board build
may link an example with a selected board target, but portable example logic
must not contain vendor SDK calls.

## `tests/`

Contains verification code and test data. Production code must not depend on
this tree.

### `tests/unit/`

Tests individual portable components in isolation. Expected coverage includes
values, graph validation, connection rules, block behaviour, timers, state,
boundary values, malformed inputs, and resource exhaustion.

Unit tests normally run on the host and should be fast enough for every local
change.

### `tests/integration/`

Tests several portable components together. Typical cases include loading and
validating a complete flow, running multiple cycles with deterministic time,
capability registration, flow replacement, execution overruns, and diagnostic
events.

Golden flows and expected traces belong here when they are independent of a
physical device.

### `tests/hardware/`

Contains firmware and host-side procedures for the common board conformance
suite. Tests should reuse the same flow definitions across devices and isolate
only the physical resource mapping.

Typical contents include:

- On-device conformance test runner
- Board-specific fixture mappings
- Serial protocol for reporting results
- Host scripts that flash, reset, stimulate, and observe devices
- Expected timing tolerances
- Loopback or wiring descriptions

A successful compile is not a hardware test. Record observed device results,
tool versions, and applicable board revisions.

## `third_party/`

Contains imported source or binary boot components that are intentionally kept
in the repository. Every component requires clear provenance.

A typical component folder contains:

```text
third_party/<component>/
  LICENSE
  README.microflow.md
  upstream source or artifact
  local patches, if any
```

`README.microflow.md` should record:

- Upstream project and source URL
- Exact version, tag, or commit
- Licence
- Why the component is required
- Files imported or generated
- Local modifications
- Reproduction and update procedure

Do not copy an entire vendor SDK when only register headers or a boot-stage file
are required. Host tools installed outside the repository should be pinned by
documentation or build configuration rather than copied here unnecessarily.

## `docs/`

Contains detailed documentation that would overwhelm the README. Likely topics
include:

```text
docs/
  architecture.md
  flow-format.md
  execution-model.md
  capabilities.md
  memory-model.md
  porting-guide.md
  board-support.md
  debugging.md
  target-status.md
```

Architecture documents explain current implemented behaviour, while
`PLANS.md` explains intended future work. Keep that distinction explicit.

Per-board wiring and flashing instructions stay with the board README unless a
shared document materially reduces duplication.

README files below the repository root should remain local in scope. For
example, a board README may describe its schematic assumptions, wiring,
flashing, debugging, and known limitations, but should reference this document
for the general purpose of `boards/`, `soc/`, `ports/`, and other shared layers.

## Generated and local files

Keep builds out of the source tree, normally under `build/<preset>/` as selected
by CMake presets. Generated files may include:

- Object files and static libraries
- ELF, BIN, UF2, and Espressif image files
- Linker map files
- Configured linker scripts and headers
- Size reports
- Test executables and test reports
- `compile_commands.json`

These are build outputs and should normally be ignored by version control.
Never place generated headers into `include/microflow/`; generate them into the
build tree and expose their directory only to the target that needs them.

## Choosing where new code belongs

Use these questions in order:

1. **Is it part of the public library contract?** Put its declaration in
   `include/microflow/` and implementation in the appropriate `src/` folder.
2. **Is it portable engine, block, or runtime behaviour?** Put it under
   `src/core/`, `src/blocks/`, or `src/runtime/`.
3. **Does it depend only on CPU instructions or ABI?** Put it in `ports/`.
4. **Does it access registers present on the microcontroller?** Put it in
   `soc/<soc>/`.
5. **Does it describe fitted components, pins, or a schematic?** Put it in
   `boards/<board>/`.
6. **Does it only compose build targets?** Put it in `cmake/` or the owning
   folder's `CMakeLists.txt`.
7. **Is it demonstration code?** Put it in `examples/`.
8. **Does it exist only to verify behaviour?** Put it in `tests/`.
9. **Was it imported from elsewhere?** Put it in `third_party/` with complete
   provenance, unless it is an external host tool that should remain external.
10. **Is it explanatory rather than executable?** Put it in `docs/` or the
    relevant board/component README.

When code appears to belong in two layers, define a narrow interface at the
boundary. For example, a timer function block belongs in `src/blocks/`, the
monotonic-time contract belongs in the common runtime interface, the hardware
timer driver belongs in `soc/<soc>/`, and the timer's physical clock source or
board override belongs in `boards/<board>/`.
