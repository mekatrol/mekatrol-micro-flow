# Output Expander Rust-to-C Firmware Migration Plan

## 1. Goal

Replace the CH32V003 output-expander firmware with production-oriented C and
small, reviewed assembly where startup, interrupt entry, or an instruction-level
hardware requirement makes assembly appropriate. The migration must not change,
remove, or weaken the working Rust firmware until the C firmware has reached
feature parity and passed the release gates in this document.

This is a parallel implementation, not an in-place translation. During the
migration:

- `Cargo.toml`, `Cargo.lock`, `.cargo/`, `rust-toolchain.toml`, the custom Rust
  target, and `src/*.rs` remain the buildable Rust reference implementation.
- New C firmware has separate source, test, build, linker, startup, and tool
  paths. It must not emit files into Cargo's `target/` directory.
- Protocol and persistent-storage formats are language-neutral contracts. The C
  implementation must interoperate with the existing Python tool and match Rust
  test vectors; it must not silently introduce a C-only protocol version.
- A feature is ported only when its C host tests, firmware build, and applicable
  hardware tests pass. Completing a source file is not completion evidence.
- The C project owns the canonical `output-expander/` name from the start. The
  Rust project is retained as `output-expander-rust/`, explicitly identifying
  it as the legacy/reference and recovery implementation. Naming does not make
  the initial C image production-ready; the release gates still do that.

The functional requirements remain in
`docs/FIRMWARE_IMPLENTATION_PLAN.md`, `docs/protocol.md`, and
`docs/CH32V003_WS2812_and_EEPROM_AI_Tutorial_Prompts.md`. Those documents contain
some Rust/HAL-specific implementation notes; use their hardware mappings,
timing, safety, and behavior requirements while replacing HAL-specific calls
with verified C register or vendor-header operations.

## 2. Migration principles

### Preserve a known-good reference

Record the Rust baseline before writing C: commit identifier, clean build and
test results, ELF section sizes, protocol vectors, observed GPIO behavior, and
hardware revision. Keep a known-good Rust ELF and its build instructions
available for A/B testing. Generated binaries should be retained as test
artifacts, not committed unless repository policy explicitly permits it.

Do not share implementation source between Rust and C. Share only externally
observable specifications and generated/test-vector data. This prevents a
change intended for the C port from breaking the Rust recovery firmware.

### Keep portable logic separate from CH32V003 hardware

Follow the useful boundary in `core-controller`: portable ISO C11 modules must
not include CH32V003 headers, while the board/SoC layer owns registers, pins,
interrupts, startup, and linker details. Portable modules must compile and run
on the host with strong warnings and sanitizers.

Prefer static storage, fixed capacities, explicit ownership, and bounded work.
There is no heap. Interrupt handlers only capture bounded hardware events or
advance explicitly designed peripheral state. The foreground owns protocol
decoding, dispatch, output decisions, persistent-configuration policy, LED
policy, and the sole watchdog feed point.

### Pin every production dependency

The C port is not complete merely because it removed Cargo crates. Before
selecting a device support package, startup source, linker script, programmer,
or register header, record its exact source, version or commit, licence, files
used, local modifications, and update procedure. Prefer project-owned startup,
linker, and narrowly scoped register access over an unreviewed framework.

Do not choose between WCH headers/examples, a small third-party support package,
or project-owned definitions by convenience alone. Phase 0 must audit each
candidate for CH32V003F4P6/RV32EC correctness, licence, maintenance, startup and
interrupt ABI, linker layout, and reproducibility. No network fetch may occur
during a normal production build.

Pin **xPack GNU RISC-V Embedded GCC 14.3.0-1** as the production cross-toolchain.
The build uses the `riscv-none-elf-gcc`, `riscv-none-elf-objcopy`,
`riscv-none-elf-objdump`, and `riscv-none-elf-size` executables from that exact
distribution. The baseline target options are `-march=rv32ec_zicsr` and
`-mabi=ilp32e`; Phase 0 must compile, disassemble, and run the smoke image to
prove that all emitted instructions and ABI behavior are valid for the
CH32V003 QingKe V2A core.

Do not use the old WCH-patched GCC as the normal compiler and do not depend on
its proprietary `interrupt("WCH-Interrupt-fast")` attribute. Initially disable
the CH32 hardware prologue/epilogue facility and use project-owned assembly
startup/vector code plus small reviewed interrupt entry/exit wrappers calling C
handlers. Any future decision to enable the hardware prologue/epilogue requires
a separate ABI design, generated-code audit, hardware tests, and toolchain
decision; it must not silently change the pinned compiler.

The `core-controller` Pico 2 RISC-V toolchain file is a structural pattern only.
Its `riscv32-unknown-elf-*` executable names and RP2350 assumptions are not the
CH32V003 configuration. Installation and verification of the pinned compiler
are documented in `docs/PROJECT_SETUP.md`.

## 3. Parallel repository layout

Start the migration with a history-preserving repository move of the complete
existing project from `output-expander/` to `output-expander-rust/`. Do not
partially move its Cargo files or sources. Verify the Rust build from its new
location before creating the new canonical C project.

Use sibling projects:

```text
output-expander/                # canonical C product
  CMakeLists.txt
  CMakePresets.json
  cmake/
    toolchains/
    warnings.cmake
    sanitizers.cmake
  src/
    main.c                     # fixed-board firmware entry point
    ch32v003.c, ch32v003.h     # registers, clock, GPIO and peripheral drivers
    startup.S, ch32v003.ld     # reset/runtime initialization and memory layout
    *.c, *.h                   # portable application and protocol modules
  tests/
    vectors/                   # language-neutral protocol/config vectors
    unit/                      # native host tests
  third_party/                 # only reviewed, pinned source/header artifacts
  tools/
    build.sh, build.ps1
    clean.sh, clean.ps1
    flash.sh, flash.ps1
    test.sh, test.ps1
    device_tool.py
  docs/                        # canonical product/C implementation docs

output-expander-rust/           # legacy/reference and recovery firmware
  Cargo.toml
  Cargo.lock
  .cargo/
  src/
  docs/                        # retained Rust-specific historical/build docs
```

The C product targets one fixed CH32V003F4P6 board, so its production source
directory is intentionally flat. Do not recreate generic `soc/`, `boards/`, or
`core/` layers unless the product genuinely gains another hardware target.

Use `output-expander/build/<preset>/` for all CMake output. Rust continues to
use `output-expander-rust/target/`. This permits several C presets to coexist
and makes clean operations precisely scoped. Do not introduce a repository-root
command that deletes both projects during the parallel period.

Move `tools/device_tool.py` and the language-neutral protocol, hardware, and
release requirements into the canonical C project, preserving their Git
history where practical. Keep Rust-only Cargo/build/tutorial material with the
legacy project. Where a document mixes product requirements and Rust/HAL notes,
make the C copy authoritative for product behavior and retain the original in
the legacy project for implementation history. Avoid two independently edited
copies of `protocol.md`.

Suggested initial presets are:

| Preset | Purpose |
|---|---|
| `host-debug` | Portable unit tests with supported sanitizers |
| `host-release` | Optimized portable tests and warning check |
| `ch32v003-debug` | Debuggable CH32V003 ELF/map/BIN build |
| `ch32v003-release` | Size-optimized production candidate |

Add separate HSI and external-crystal firmware presets only when their startup
and clock code genuinely differ. Do not hide clock selection in a developer's
environment.

## 4. Required build-system behavior

Reuse the `core-controller` pattern: CMake 3.25 or newer, Ninja, named presets,
an explicit cross-toolchain file, compilation databases, warnings as errors,
host sanitizers, shell and PowerShell wrappers, and explicit flashing. Adapt the
pattern to CH32V003 rather than copying Pico SDK or UF2 assumptions.

The wrapper contract is:

- `build` validates tools, optionally formats, configures one preset, and builds
  incrementally; `--clean` removes only that preset's directory first.
- `clean` removes only `output-expander/build/`. It never removes
  `output-expander-rust/target/`.
- `test` configures/builds the selected host preset and runs CTest with failure
  output. It supports selecting one test during development.
- `flash` builds a flashable C preset if needed, validates the exact ELF path,
  identifies the attached WCH-LinkE/target, and invokes a pinned programmer.
  Flashing is never an implicit side effect of an ordinary build.
- Every wrapper resolves paths relative to itself, rejects unknown targets and
  options, and fails on missing tools or stale/missing artifacts.
- Linux shell and Windows PowerShell wrappers expose the same target names and
  semantics. If a platform cannot yet flash, it must fail with a documented
  unsupported result rather than pretending to succeed.

Firmware linking must produce at least ELF, map, and raw BIN artifacts. The
build must print Flash and static SRAM use and fail if the image exceeds 16 KiB
Flash or 2 KiB SRAM. Add configurable warning thresholds before feature work
begins. Keep debug symbols in ELF artifacts even when the flash image is
stripped. Generate and inspect disassembly during startup/IRQ bring-up.

## 5. Phased implementation

Each phase begins from passing results from the previous phase. Within a phase,
port one cohesive feature at a time, add tests, build both implementations, and
record evidence before proceeding.

### Phase 0 — Clean, build, flash, and test foundation

**Implementation status (2026-10-28): software complete; physical evidence
pending attached hardware.** The Rust project was moved intact to
`output-expander-rust/`; the canonical C project now provides independent host
and CH32V003 presets, pinned-toolchain enforcement, startup/linker/register
ownership, memory limits, shell/PowerShell wrappers, sanitizer-backed host
testing, and the safe PC0 1 Hz smoke image. Clean debug/release images and ELF
layout/disassembly were verified. See [PHASE0_EVIDENCE.md](PHASE0_EVIDENCE.md)
and [DEPENDENCIES.md](DEPENDENCIES.md). No WCH-LinkE was attached during this
work, so the flash/reset/observed-heartbeat/Rust-recovery rows remain explicitly
pending rather than being reported as passed. `tools/flash.sh` and
`tools/flash.ps1` implement the complete explicit flash path for that final
bench run.

This is the first implementation step. It must be completed before translating
application features.

1. Capture the Rust baseline with a clean host test, release firmware build,
   section/stack report, and known-good hardware flash. Record exact commands,
   tool versions, board revision, programmer, and observed result.
2. Move the complete existing directory to `output-expander-rust/` with Git,
   update only repository-level path references needed to find it, then repeat
   its clean test/build/flash checks from the new path. Do not modify Rust
   behavior during this move.
3. Create a new canonical `output-expander/` root and move the authoritative
   language-neutral product documents and Python device tool into it. Check all
   relative links and leave Rust-specific instructions with the legacy project.
4. Decide and document the C dependency boundary: compiler/binutils version,
   register headers, startup ownership, linker script provenance, programmer,
   and licences. Vendor or otherwise pin every required build input.
5. Create the root C project with host and CH32V003 CMake presets, an explicit
   xPack GCC 14.3.0-1 `riscv-none-elf-*` toolchain configuration using
   RV32EC/ILP32E, C11 warnings, host sanitizers, and independent build
   directories. Reject a different cross-compiler version unless an intentional
   toolchain update changes the pin and repeats the generated-code/hardware
   validation.
6. Implement matching shell and PowerShell `clean`, `build`, `test`, and
   `flash` wrappers using the `core-controller/tools` behavior as the pattern.
7. Add the smallest firmware image: reset/startup, vector table, linker script,
   clock initialization, `main`, and a visible but safe board heartbeat. All
   eight active-low outputs must be driven HIGH/OFF before the heartbeat starts;
   the RS-485 driver must remain disabled/receive-safe.
8. Add a minimal portable C unit test to prove host CTest discovery, warning
   flags, sanitizer execution, and wrapper error propagation.
9. Prove, from a clean tree, clean host build/test, clean firmware debug/release
   builds, artifact/size reporting, flash, reset, and heartbeat. Inspect ELF
   headers, sections, reset vector, stack bounds, and generated instructions.
10. Document recovery by reflashing the Rust reference image after the C smoke
   image. This proves parallel development has not stranded the board.

Exit gate: a new checkout can build and test C without Rust tooling, can build
Rust without the C toolchain, can flash either image explicitly, and cleaning
one implementation does not touch the other.

### Phase 1 — Portable constants, outputs, and configuration codec

Port `board.rs`, `outputs.rs`, and the pure parts of `config.rs` into portable C.
Define fixed-width types and compile-time assertions for every serialized size.
Keep logical output masks independent of active-low electrical levels.

Create shared golden vectors for:

- all eight output bits and all-off/all-on masks;
- default and boundary configurations;
- the exact 64-byte record layout and byte order;
- CRC-32 known values, valid decode, corrupt records, and reserved-byte rules.

Do not invent the planned EEPROM sequence field while translating the current
Rust codec. First reproduce the current format exactly; introduce the final
dual-slot schema later as an explicit, versioned feature with migration tests.

Exit gate: host C tests match Rust-produced vectors byte-for-byte, and both Rust
and C suites still pass.

### Phase 2 — Protocol framing and receive state machine

Port `protocol.rs`: packet representation, CRC-16/CCITT-FALSE, COBS encode and
decode, bounds checking, and the delimiter-driven frame receiver. Use fixed
arrays sized from the protocol constants and return explicit status codes; do
not use packed C structs or cast byte buffers to protocol objects.

Run the same language-neutral corpus against Rust, C, and
`tools/device_tool.py`, including the `123456789` CRC vector, embedded zeroes,
maximum frames, malformed COBS, truncation, bad length/type/version/CRC, and
recovery after a delimiter. Add sanitizer-backed malformed-input iteration or
fuzzing on the host.

Exit gate: encoded frames and accept/reject decisions match across all three
implementations, with no sanitizer or bounds failures.

### Phase 3 — Command dispatcher and application state

Port `dispatcher.rs`, including addressing, broadcast restrictions, UID-targeted
assignment, response/error construction, output-change notification, heartbeat
classification, configuration-change notification, and reset intent. Keep the
dispatcher portable and free of GPIO, UART, EEPROM, timer, and watchdog access.

Test every command and invalid length/value, wrong destination, response/error
input, UID match/mismatch, broadcast no-response rule, and repeated request
semantics. Preserve the protocol's commit-first/activate-second requirement:
the initial C dispatcher may request a commit, but it must not report a
persistent change as successful until the later storage layer confirms it.

Exit gate: Rust and C command outcomes and response bytes match for the shared
test corpus.

### Phase 4 — Safe GPIO and monotonic time

Port `output_gpio.rs` and `monotonic.rs` into the CH32V003 SoC/board layer.
Implement clock initialization and SysTick directly from verified register
definitions. Establish documented wraparound-safe time comparisons.

On hardware, prove that PC0, PC3, PC4, PC5, PD0, PD2, PD3, and PD4 start HIGH
and change coherently according to the logical mask. Capture reset, programming,
brownout, and all eight bit transitions on a logic analyser. Confirm PA1/PA2,
PD1, and PD7 remain reserved as required.

Exit gate: GPIO and timing tests pass, startup cannot energize a load, and
measured clocks agree with the selected HSI build within documented tolerance.

### Phase 5 — USART1, RS-485 direction, and interrupt RX queue

Port `rs485.rs`, `serial.rs`, `usart.rs`, and `irq_rx.rs`. Implement the exact
QingKe interrupt ABI in C/assembly as required by the selected compiler. Keep
the ISR bounded: capture RX bytes/errors into a fixed ring and return. Document
single-producer/single-consumer ownership, volatile/atomic requirements,
critical sections, overflow behavior, and memory-order assumptions.

Transmit only for valid addressed responses. Assert DE, send bounded data, wait
for USART transmission-complete rather than data-register empty, then return to
receive. Reconcile the current-board separate DE and `/RE` wiring with the
planned production tied PC6 signal through explicit board variants; do not hide
the difference in application code.

Exit gate: C echoes/serves protocol traffic at 19,200 8N1, releases the bus at
TC, recovers after UART errors/overflow, and survives sustained receive traffic
without corrupting outputs or frames.

### Phase 6 — Integrated protocol loop and Python interoperability

Connect the portable receiver and dispatcher to GPIO and RS-485 in the C
foreground loop. Retain fixed receive/response buffers and keep hardware effects
outside parsing. Integrate the existing Python device tool without changing the
wire format.

Run an A/B hardware suite: flash Rust, execute and capture every supported
command; flash C and repeat with the same host commands and expected bytes.
Include PING/UID, status, set/get outputs, malformed traffic, wrong address,
broadcast behavior, reset acknowledgment, and output safety across reflashing.

Exit gate: the host cannot distinguish Rust and C for implemented protocol
features except for deliberately documented firmware-version data.

### Phase 7 — Watchdog, communication timeout, and fault behavior

Port `watchdog.rs` and complete communications-timeout behavior from the
firmware plan. The independent watchdog is fed only at the final low-priority
point in the foreground loop after meaningful progress. No ISR, wait loop, LED
completion path, or EEPROM operation may feed it.

Test timer wraparound, timeout disabled/enabled, startup grace, heartbeat
refresh, fallback outputs, recovery, deliberate reset after acknowledgment,
foreground stalls, interrupt storms, UART noise, and stuck peripheral waits.

Exit gate: fault injection produces the documented safe output state and reset
behavior, with reset cause/status observable where supported.

### Phase 8 — WS2812 policy and timer/PWM/DMA driver

Port the pure color/status policy from `status_led.rs`, then replace the current
Rust CPU-driven `ws2812.rs` mechanism with the production architecture already
required by the firmware plan: PC7/TIM1_CH2 remap group 1, DMA1_CH3 compare
requests, 24 GRB duty values, a completion path, and a bounded reset-low state.
Use the current-board PC1/TIM2_CH4/DMA1_CH7 mapping only as an explicit bring-up
board variant.

Use assembly only if a verified compiler/ABI limitation requires a tiny IRQ or
register sequence. Timer/DMA hardware, not instruction timing or per-bit
interrupts, owns waveform accuracy.

Exit gate: scope captures prove bit period, zero/one high times, 24 complete
pulses, final pulse, reset-low interval, correct color order, latest-color-wins
behavior, and coexistence with sustained USART traffic and watchdog faults.

### Phase 9 — I2C EEPROM and power-loss-safe configuration store

Do not begin the EEPROM driver until the exact SOT-23-5 ordering code and
datasheet establish pinout, address, page size, voltage, timing, ACK polling,
and endurance. Implement bounded I2C1 operations on PC2/PC1 in the SoC layer,
then implement EEPROM page operations and the portable two-slot store policy as
separate modules.

Add the sequence number and final 64-byte schema through an explicit format
decision. Test slot validation, wraparound ordering, blank/corrupt records,
inactive-slot selection, page boundaries, NACK, stuck bus, timeout, interrupted
writes, readback mismatch, repeated commits, schema mismatch, and power cycling.
Configuration and address changes commit and verify first, activate second.

Exit gate: destructive power-cut testing demonstrates that at least one valid
record survives every interruption point, and failed commits never produce a
success response or activate uncommitted settings.

### Phase 10 — Production board, soak, and release candidate

Integrate the final pin map, optional crystal build, clock validation, EEPROM,
WS2812, RS-485, outputs, reset/programming access, and both output-stage board
variants. Resolve obsolete PC7 button/input behavior in the protocol and docs;
production PC7 belongs to the WS2812 and cannot also be sampled as a button.

Run long-duration multi-device RS-485, noise, power-cycle, brownout, watchdog,
EEPROM endurance-policy, and simultaneous peripheral tests. Measure release
Flash, `.data`, `.bss`, worst-case stack high-water mark, IRQ nesting, timing,
and remaining growth margin. Review disassembly and the map for accidental
runtime/library pulls, writable executable sections, unbounded library calls,
and unsupported instructions.

Exit gate: all C release gates pass on each supported hardware revision, the C
image can be reproduced from a clean checkout without network access, and the
Rust reference can still be built and flashed.

### Phase 11 — Production approval and later Rust retirement

The C project already owns the canonical name, but it becomes the approved
manufacturing/release firmware only through a separate production decision.
Retain `output-expander-rust/` for at least one C release and recovery cycle.
Update repository README, build/setup docs, release checklist, CI,
manufacturing flashing instructions, firmware-version policy, and
licence/dependency inventory together when production approval is granted.

Only remove Rust files in a later explicit change after:

- released C firmware has passed field or extended soak evidence;
- no manufacturing, recovery, protocol-vector, or documentation workflow
  depends on Cargo;
- useful Rust test vectors and behavioral findings have been preserved in
  language-neutral tests/docs; and
- the last Rust source revision and known-good binary are recoverable from
  version control/release artifacts.

## 6. Rust-to-C feature map

| Rust source | C destination/responsibility | Primary proof |
|---|---|---|
| `board.rs` | board config/constants | compile-time assertions and pin review |
| `outputs.rs` | portable logical outputs | host unit vectors |
| `output_gpio.rs` | board GPIO adapter | startup and eight-channel captures |
| `config.rs` | record codec/CRC and later store schema | byte-identical vectors |
| `protocol.rs` | packet/COBS/CRC/frame receiver | cross-language corpus |
| `dispatcher.rs` | portable command state machine | outcome/response parity |
| `irq_rx.rs` | bounded SPSC UART event ring | host ring tests and overflow injection |
| `rs485.rs`, `serial.rs`, `usart.rs` | USART1 and transceiver driver | DE/TC captures and soak |
| `monotonic.rs` | SysTick timebase | wraparound tests and clock measurement |
| `uid.rs` | electronic-signature reader | 12-byte A/B PING match |
| `watchdog.rs` | IWDG and foreground health policy | deliberate stalls/reset evidence |
| `status_led.rs` | portable LED policy | color/priority vectors |
| `ws2812.rs` | timer/PWM/DMA state machine | scope plus serial coexistence |
| `main.rs` | board init and foreground orchestration | full A/B behavior suite |
| planned `eeprom.rs` | bounded I2C/EEPROM driver | bus faults and datasheet timing |
| planned `config_store.rs` | portable dual-slot policy | power-failure matrix |

## 7. Verification matrix and evidence

Maintain a migration matrix in the release checklist with one row per feature
and these states: specified, C host-tested, C firmware-built, Rust/C parity
checked, hardware-tested, soak-tested, and release-approved. Link each hardware
result to board revision, firmware commit, compiler version, preset, programmer,
test procedure, measurement/capture, duration, and pass/fail outcome.

Every phase must run at least:

1. Rust host tests and `cargo firmware` to detect accidental regression.
2. C `host-debug` tests with sanitizers and `host-release` tests.
3. C CH32V003 debug and release builds with size reports.
4. The phase-specific cross-language vectors or hardware procedure.

CI may build and test both implementations in parallel, but hardware flashing
remains explicit and serialized per attached board. Treat compilation as proof
only of compilation; all electrical, timing, persistence, and fail-safe claims
require recorded hardware evidence.

## 8. Immediate next action

Implement Phase 0 only. Start by recording the current Rust baseline, move the
whole existing project to `output-expander-rust/`, verify it there, and create
the new canonical `output-expander/` C build skeleton and wrappers. Do not
translate protocol, drivers, or application modules until a clean checkout can
build, test, flash, and recover both the C smoke image and the unchanged Rust
image.
