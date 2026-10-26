# CH32V003F4P6 RS485 Output Expander — Implementation Plan

Status: implementation specification and staged acceptance plan. Intended for a human developer or coding agent (Codex). **Do not treat unverified HAL APIs, pin mappings, or silicon details as established facts; validate them against the pinned sources before coding.**

## 0. Project context and objective

Build production-oriented, `no_std`, heap-free Rust firmware for the WCH CH32V003F4P6 (TSSOP-20), driving eight ULN2803A inputs, communicating via half-duplex RS485 (SP485EEN-L/TR or compatible) at 19,200 bit/s, and indicating status through one WS2812 LED. Use interrupt-driven communications and timers; keep `main` primarily a low-priority status LED and watchdog service loop. Implement Modbus RTU (standard functions wherever possible), CRC-16, safe UID-assisted address commissioning, persistent configuration in a reserved final 64-byte Flash page, and an independent watchdog.

Existing known-good project:

- Package: `output-expander`, Rust edition 2024.
- Target: `riscv32ec-unknown-none-elf.json`.
- `.cargo/config.toml`: `firmware = "build --release -Z build-std=core"`; `flash = "run --release -Z build-std=core"`; runner `wlink -v flash`; linker `-Tlink.x`; `json-target-spec = true`.
- `ch32-hal` Git `https://github.com/ch32-rs/ch32-hal` with features `ch32v003f4p6`, `memory-x`, `rt`; observed checkout revision `bbcabaa` (confirm in `Cargo.lock`).
- `qingke-rt = 0.8.2` with `highcode`; `panic-halt = 1.0`.
- Previously compiled and ran: `hal::init`, `Output::new`, TIM2 low-level timer with `set_frequency(Hertz(...))`, `clear_update_interrupt`, `enable_update_interrupt(true)`, `start`, `#[qingke_rt::interrupt] fn TIM2()`, `hal::interrupt::InterruptExt`, `hal::interrupt::TIM2.enable()`, `pac::TIM2.intfr().modify(|r| r.set_uif(false))`.
- **Important architecture constraint:** target does not support atomic read-modify-write operations (`AtomicU32::fetch_add`, `AtomicBool::swap`). `AtomicBool::load/store` work but are not an atomic read-and-clear pair. Prefer interrupt-masked critical sections or single-writer data structures.

### Definition of done

1. Reliable Modbus RTU slave at 19,200 bit/s, 8E1 by default, CRC verified, documented register map, robust framing, correct RS485 DE switching.
2. Eight outputs controllable independently or as a bitmask, known safe startup and watchdog/reset state.
3. Device UID readable, unconfigured devices commissioned without multiple devices responding simultaneously; standard Modbus slave IDs 1–247, ID 0 broadcast only.
4. Configuration survives power cycling; firmware linker excludes the final 64 bytes; writes verified; power-failure behavior documented and tested.
5. WS2812 status updates are low priority and cannot corrupt or starve RS485 traffic.
6. Independent watchdog serviced only when system health conditions are satisfied; deliberately hung main loop resets device safely.
7. `cargo firmware` and `cargo flash` work with the existing toolchain; all features fit within 16 KB Flash and 2 KB SRAM with recorded size headroom.

## 1. Mandatory discovery and validation (Stage 0)

Before editing implementation, inspect the repository and locked dependencies:

- `Cargo.toml`, `Cargo.lock`, `.cargo/config.toml`, `riscv32ec-unknown-none-elf.json`, `src/main.rs`, generated linker scripts and build output.
- HAL source at the locked revision: UART/USART driver, interrupt extension, timer, Flash controller, watchdog, GPIO, DMA, clocks, critical-section facilities, chip PAC, UID and reset-cause registers.
- Manufacturer CH32V003F4P6 datasheet and CH32V003 reference manual, plus SP485EEN and ULN2803A datasheets. Record citations/section names in project notes. Verify peripheral routing, alternate functions, remapping, electrical limits, UID register addresses/length, erase/write granularity and Flash endurance, and whether TIM2 and USART1 can run concurrently with chosen pin mapping.
- Confirm physical pin numbers against the **manufacturer TSSOP-20 pinout** before wiring. The provisional pin mapping below is not PCB-authoritative; previous discussion contained unverified pin diagrams.
- Check `ch32-hal` ownership model, whether USART interrupt and DMA APIs are available and whether Flash operations require RAM-resident code or special unlock/erase sequences.
- Verify exact mechanism for overriding HAL-generated `memory.x` when the `memory-x` feature is active. Inspect actual linker map; **do not assume that placing a local `memory.x` overrides generated memory layout**.
- Inspect Flash tool (`wlink`) erase behavior. Full-chip erase may destroy saved configuration even if linker excludes it.
- Check clock tree, UART baud error, TIM2 timer input clock, and IWDG low-speed oscillator tolerance.

**Stage 0 exit:** commit a `docs/hardware-validation.md` listing verified pins, HAL methods, memory map, UID details, reset/watchdog features, electrical assumptions and unresolved issues. Stop and ask for hardware confirmation if pinout or output polarity is ambiguous.

## 2. Proposed electrical interfaces and pin plan

Tentative GPIO names from earlier discussion (verify exact physical pins and USART mapping in Stage 0):

| Function | Proposed MCU signal | Notes |
|---|---|---|
| ULN2803A IN1..IN8 | PC0, PC3, PC4, PC5, PD0, PD2, PD3, PD4 | Eight separate GPIO; mapping can change before PCB finalization |
| USART1 TX / RX | PD5 / PD6 | Confirm default/remapped function in selected HAL configuration |
| RS485 DE and /RE | PC6 | Tie DE and active-low /RE together: LOW = receive, HIGH = transmit |
| WS2812 DIN | PC1 | One LED, existing status LED pin; verify voltage compatibility and waveform generation |
| SWIO | PD1 | Reserve for programming/debugging |
| NRST | PD7 | Prefer retaining reset functionality |

SP485EEN: MCU TX→DI, RO→MCU RX, PC6→DE and /RE, A/B to bus. DE and /RE tied is valid for mutually exclusive transmit/receive. Default PC6 LOW at boot, preferably with external pull-down to avoid enabling driver during reset. Add RS485 termination only at physical bus ends, bus biasing as required by topology, appropriate common reference, and protection as dictated by environment. Check 3.3 V transceiver compatibility, input thresholds and loading. Never drive DE HIGH before UART and GPIO are configured. On transmit, assert DE before first start bit, wait for **USART transmission complete (TC), not merely TX buffer empty**, then deassert DE and return to receive. Account for DE setup/hold timing and possible RX echo.

ULN2803A: MCU HIGH energizes the corresponding Darlington sink channel; MCU LOW turns it off. This is an open-collector low-side output, not a push-pull voltage source. Connect external load between positive supply and ULN output. For inductive loads connect COM to the positive load supply to use clamp diodes; verify current, power dissipation, grounding and supply design. Outputs should default OFF (GPIO LOW) at reset/startup and remain OFF until initialized. Document whether loads are safety-critical; do not assume this firmware alone provides functional safety.

WS2812: verify LED supply voltage and whether 3.3 V data meets VIH at that supply. Use a level shifter if needed. Budget ~24 bits × 1.25 µs ≈ 30 µs for a single RGB LED, plus reset/latch interval (the earlier 720 µs estimate was incorrect). Do not disable interrupts long enough to lose UART bytes. Validate timing on a scope/logic analyzer.

## 3. Software architecture and ownership

Suggested modules (adapt to actual HAL APIs and code-size budget):

```
src/
  main.rs          // initialization, low-priority LED/watchdog loop
  board.rs         // pin mapping, GPIO initialization and safe outputs
  uart.rs          // USART RX/TX ISR, bounded buffers, TC and DE state machine
  modbus.rs        // RTU parser, CRC16, functions, exceptions
  registers.rs     // coils and register map, validation
  timer.rs         // TIM2 monotonic tick and RTU inter-frame timing
  commissioning.rs // UID selection/discovery and address assignment
  config.rs        // persistent config format, validation, flash I/O
  uid.rs           // silicon unique identifier access
  watchdog.rs      // IWDG setup, feed, health gating
  status_led.rs    // nonblocking WS2812 status rendering
```

Keep ISR execution short and bounded. USART RX ISR reads received byte, handles UART error flags, and enqueues byte with timestamp/frame boundary information. TX ISR feeds bytes, transitions to waiting-for-TC, then releases DE. Timer ISR updates bounded time counters and flags. Parsing and application state transitions should run in a high-priority cooperative worker or carefully bounded interrupt-deferred work; **a main loop devoted only to status LED is incompatible with deferred parsing unless another worker exists**. Prefer a short main-loop service call `service_comms()` ahead of LED work, then LED/watchdog as low priority, or implement a safe ISR-driven parser if measured ISR cost is acceptable. Explicitly document chosen scheduling model.

Avoid `static mut` shared unsafely. Use critical sections and static buffers with single-producer/single-consumer ownership; no heap. On this target, do not use unavailable atomic RMW operations. Size buffers based on maximum supported Modbus frame (e.g., 64–128 bytes if restricting request lengths); reject oversize frames and document limits. Standard Modbus ADU can be up to 256 bytes, so if claiming full standard compliance, provide adequate buffer and verify SRAM use; otherwise explicitly document supported function-specific maximums.

## 4. Communication and Modbus RTU contract

- Default: **19,200 baud, 8E1** (8 data bits, even parity, one stop bit); make framing configurable only if necessary. Document 8N2 alternative if master requires it. Ensure USART peripheral is set to deliver 8 actual data bits with parity (device register nomenclature may differ).
- RTU frame: `[slave_id, function, payload..., CRC_LO, CRC_HI]`.
- CRC16/Modbus: init `0xFFFF`, reflected polynomial `0xA001`, low byte transmitted first. Include unit tests with known standard vector `"123456789" -> 0x4B37` and full request/response fixtures.
- 3.5-character silent interval separates frames; 1.5-character intra-frame gap invalidates a frame. At 19,200 8E1, character time = 11/19200 = 572.9167 µs, t1.5 ≈ 859.4 µs and t3.5 ≈ 2005.2 µs. **Modbus serial-line guidance commonly specifies fixed t1.5=750 µs and t3.5=1.750 ms for baud rates >19,200; at exactly 19,200 use character-based intervals.** Verify interpretation with master. TIM2 at 1 ms is too coarse; use finer timer resolution (e.g., 50–100 µs) or UART idle detection plus timer compare. Avoid long, high-rate ISRs if possible.
- Process only addressed frames (1–247), plus defined broadcasts to 0. **Never respond to address 0.** Never treat 0 as an ordinary slave ID.
- Standard exception codes: 0x01 illegal function, 0x02 illegal data address, 0x03 illegal data value, 0x04 slave device failure, as applicable; exceptions not returned to broadcasts.
- Functions:
  - `0x01` Read Coils, addresses 0–7 (LSB first in response byte).
  - `0x05` Write Single Coil (0xFF00 ON / 0x0000 OFF), validate address/value.
  - `0x0F` Write Multiple Coils, contiguous within 0–7, validate quantity and byte count; update output bitmask coherently.
  - `0x03` Read Holding Registers (config, bounded ranges).
  - `0x04` Read Input Registers (UID, firmware and status).
  - `0x06` Write Single Register only for explicitly writable non-dangerous configuration; **address changes should require commissioning/authorization and must not silently alter an active bus identity**.
  - `0x41` vendor-specific UID commissioning, documented below. Because vendor-specific semantics are not interoperable with generic Modbus tools, isolate and document them.
- Outputs only change after a valid addressed CRC-checked command. Define behavior for invalid frames, wrong addresses, broadcast writes, watchdog reset and comms timeout.

### Proposed zero-based register map (freeze and test before implementation)

| Space | Address | Semantics |
|---|---:|---|
| Coils | 0–7 | ULN channels 1–8, 1=ON |
| Holding | 0 | Configured Modbus slave address, 1–247; read-only via normal function 0x06 unless explicitly unlocked |
| Holding | 1 | Startup output bitmask (default 0x00) |
| Holding | 2 | Communication timeout in ms, 0=disabled; default e.g. 5000 ms |
| Holding | 3 | Output bitmask on communication timeout (default 0x00) |
| Holding | 4 | WS2812 brightness (0–255; cap for current) |
| Input | 0 | Firmware major/minor packed version |
| Input | 1 | Current physical output bitmask |
| Input | 2 | Status flags (configured, timeout, errors, reset reason) |
| Input | 3–6 | 64-bit device identifier as four 16-bit words, explicitly define endian/word order |

UID representation: inspect actual silicon ID width and uniqueness before defining the final register contract; do **not** silently truncate if full UID is needed to distinguish devices. If the UID is 96 bits, expose six registers or define and justify a verified 64-bit identity. Record uniqueness assumptions and collision handling.

### Commissioning protocol (0x41) — draft, finalize before shipping

Avoid unslotted responses from multiple unconfigured devices. Proposed subcommands:

1. `QUERY_SLOT(prefix_bits, prefix_value, slot/nonce)` broadcast: only unconfigured devices whose full verified UID matches selected prefix participate; deterministic time slots or binary UID-prefix tree search prevents simultaneous replies. Include retries and collision handling; a random delay alone does not guarantee uniqueness.
2. `READ_UID` unicast to a selected device (or isolated-bus commissioning) returns full UID.
3. `ASSIGN_UID(full_uid, new_slave_id)` broadcast: **only** matching unconfigured UID commits address 1–247; no broadcast response. Controller subsequently verifies at new address and checks returned UID. For readdressing configured units, require explicit maintenance mode or addressed, UID-matched authenticated/physical commissioning policy.
4. `QUERY_STATUS` unicast returns configuration version and UID.

**Protocol design decision:** A pure UID-prefix discovery mechanism needs a way to observe collisions/empty slots reliably on RS485; implement a binary-tree slot protocol with explicit frame timings, or require only one unconfigured device on the bus. If neither can be tested robustly, defer multi-device discovery and ship isolated commissioning first. Define exact byte layouts, field order, response timing, timeout, retry rules, CRC coverage and malformed-message behavior in `docs/protocol.md` before writing code. Protect against accidental mass readdressing and duplicate IDs. Security note: CRC is not authentication; hostile devices on a shared bus can spoof commands. Consider physical commissioning enable or authorization if bus access is untrusted.

## 5. Configuration storage: last 64-byte Flash page

- CH32V003F4P6 nominal Flash: 16 KiB, `0x0800_0000..=0x0800_3FFF`.
- Reserve final 64 bytes at **`0x0800_3FC0..=0x0800_3FFF`** *only after confirming 64-byte erase granularity and flash programming mode in reference manual*.
- Linkable firmware Flash length: `0x3FC0` = 16,320 bytes, with Flash origin `0x0800_0000`. Verify with `.map` and binary size, plus a build-time assertion/check that no firmware section overlaps reserved page. Ensure boot/vector table unaffected.
- Suggested exact 64-byte v1 record:

| Offset | Size | Field | Default |
|---|---:|---|---|
| 0 | 4 | Magic `0x4D454B31` (document on-flash little-endian) | magic |
| 4 | 2 | Format version = 1 | 1 |
| 6 | 1 | Slave address, `0xFF` means unconfigured | 0xFF |
| 7 | 1 | Startup outputs mask | 0 |
| 8 | 2 | Comms timeout ms, little-endian | 5000 |
| 10 | 1 | Timeout outputs mask | 0 |
| 11 | 1 | WS2812 brightness | 32 |
| 12 | 48 | Reserved, all 0xFF | 0xFF |
| 60 | 4 | CRC32 of bytes 0..59, little-endian | computed |

- On blank/invalid record: use address state **247**, outputs OFF, safe defaults, and accept only documented commissioning commands; do not use address 0 as normal slave identity.
- On startup: read and validate magic/version/address/ranges/CRC; do not write defaults automatically at every boot.
- On config change: validate entire proposed config in RAM, prepare complete page image, wait for UART TX completion and idle bus, quiesce affected interrupts if required, unlock/erase/program/verify, restore interrupt state and respond/verify using defined protocol. Ensure watchdog timeout allows worst-case Flash operation; do not feed blindly from a stalled system.
- **Power-loss warning:** single-page erase-and-rewrite is not power-fail atomic; interrupted erase can destroy old configuration. Implement detection and safe unconfigured recovery. If retaining the previous config after unexpected power loss is required, reserve **two** erase pages with generation numbers and commit markers, changing the linker plan accordingly.
- Avoid wear: only save changed values; consider a command to commit multiple pending changes together; document endurance. Determine whether `wlink flash` performs mass erase and how to preserve config during firmware updates; do not promise preservation without test.

## 6. Timer, interrupts, watchdog and scheduling

- TIM2 supplies high-resolution time for RTU framing and optional 1 ms derived software ticks. Choose timer tick or compare strategy with measured interrupt load; do not use an inaccurate 1 ms-only frame detector.
- USART RXNE and error interrupts: bounded work, overrun/framing/parity counters, discard invalid frames; RX enabled when DE LOW.
- USART TXE and TC: interrupt-driven send and **wait for TC** before DE LOW. Add state machine: `Receiving -> Turnaround -> Transmitting -> WaitTC -> Receiving`.
- Main loop service priority: (1) process completed RTU frames/config events promptly if deferred, (2) service timeouts and outputs, (3) update status LED when bus idle, (4) feed watchdog only when health checks pass, (5) optionally idle/WFI if safe.
- IWDG: verify HAL/PAC setup and LSI clock tolerance. Choose initial nominal timeout e.g. 2 seconds, longer than worst-case allowed operations; record actual measured range. Enable only after GPIO safe state, interrupts, timer, UART and status structures are ready. Feed from low-priority loop **only if** ISR progress and main service progress are healthy (e.g. bounded heartbeat checks, allowing legitimate bus silence). Never require RX traffic to feed watchdog. If main loop hangs, watchdog should reset. Record reset cause at startup and expose in status.
- Output timeout: configurable and independently enforced using monotonic tick. On timeout set safe mask (default all OFF). Decide whether only valid addressed commands refresh timeout; recommended yes, with explicit handling of broadcast writes.
- WS2812: low-priority, event-driven status patterns; never block receive/turnaround. Preferred hardware-assisted timing if verified available without conflicting TIM2/USART; otherwise measure a bounded bit-bang implementation under UART stress. For one LED, ~30 µs data transfer plus reset time, but even ~30 µs of interrupt masking must be assessed against RX interrupt latency. Recommended patterns: unconfigured=amber slow pulse, configured/healthy=green heartbeat, valid command=brief blue flash, comms timeout=red blink, Flash fault=red fast blink. Cap brightness.

## 7. Staged implementation with acceptance gates

### Stage 0 — Hardware/HAL validation and baseline

Deliver: hardware validation notes, pinned `Cargo.lock`, baseline binary size, pinout confirmed against datasheet, verified toolchain commands. Preserve existing working TIM2 demo in git history.

Accept: `cargo firmware` succeeds; `cargo flash` works; pins and Flash page assumptions documented. No production Flash writes yet.

### Stage 1 — GPIO and safe output driver

Implement `board.rs`/`outputs.rs`: initialize all ULN inputs LOW before enabling outputs; bitmask write/read; reject channels outside 0..7; ensure no unwanted glitches at boot/reset; reserve SWIO/NRST. Add host-side unit tests for bitmask-to-pin mapping using a pure-Rust mapping function.

Accept: logic analyzer verifies eight independently addressable channels, startup OFF, polarity correct; code-size report.

### Stage 2 — USART1 + half-duplex RS485 physical transport

Implement 19,200 8E1 USART, PC6 DE control, RX ISR, TX ISR, TC completion, bounded queue, parity/framing/overrun counters and test echo/turnaround. Start with UART loopback and one master, no Modbus parser. Ensure DE LOW when idle/reset.

Accept: hundreds/thousands of stress transactions with no missing bytes; verify DE transitions relative to last stop bit on scope; no TX contention.

### Stage 3 — RTU framing + CRC + Modbus core

Implement CRC and parser, accurate t1.5/t3.5, address filtering, `0x01`, `0x05`, `0x0F`, exceptions and output control. Start with a fixed test slave ID (e.g. 1) until Flash config exists. Add tests for bad CRC, partial/oversized frames, noise, back-to-back requests, broadcasts, wrong addresses and invalid coil values.

Accept: interoperates with independent Modbus RTU master; read/write all eight outputs; no response to broadcasts; measured timing and memory footprint.

### Stage 4 — UID and read-only information registers

Verify actual UID layout and expose full ID through `0x04` input registers; add firmware/status registers and `0x03` read-only configuration view. Document byte/word order and prove ID stability across resets and uniqueness across available sample devices.

Accept: master reads stable UID and status, read-only accesses do not mutate config.

### Stage 5 — Flash reservation + configuration persistence

Implement linker exclusion, 64-byte versioned config format, CRC32, read/validate, erase/program/verify, startup fallback, bounded writes. Integrate `0x06` only for whitelisted settings with defined commit semantics. Test power loss, blank Flash, corruption, repeated writes, firmware flashing preservation. If 64-byte page cannot be safely reserved with current HAL feature, change build configuration explicitly and revalidate.

Accept: configuration survives reset and normal power cycle; binary cannot overlap config; corrupted config safely disables outputs; no writes on ordinary startup.

### Stage 6 — UID-targeted address commissioning

Finalize `docs/protocol.md` exact function `0x41` bytes, no-response broadcast assignment, UID matching and verification. Implement isolated-device commissioning first; then optional collision-safe multi-device discovery using binary UID prefix/slots and master retries. Validate duplicate-address detection/behavior and prevention of accidental mass assignment.

Accept: brand-new device can be assigned ID 1–247, survives reset, responds only to assigned ID; multiple unconfigured devices never simultaneously send unsolicited responses.

### Stage 7 — Timers, output timeout and independent watchdog

Integrate high-resolution TIM2 timebase, timeout-safe output state, IWDG health-gated service in main loop, reset-cause reporting. Confirm Flash operations fit watchdog window. Fault inject: stop main loop, stop USART progress under pending workload, corrupt frame stream.

Accept: watchdog resets deliberately stalled firmware, outputs initialize OFF on reboot, no resets under worst-case normal traffic/Flash saves, comms timeout enforces safe mask.

### Stage 8 — WS2812 low-priority status

Implement a single-LED status engine driven from low-priority loop, with documented states and brightness. Validate actual WS2812 timing and electrical levels; ensure no UART RX overrun, framing failure or DE timing errors during LED updates at max bus load.

Accept: status indications correct, Modbus soak test unchanged with LED enabled, watchdog remains healthy.

### Stage 9 — Integration, robustness and release

Run continuous RS485 soak, multiple slaves, noise/CRC tests, power cycling, firmware updates, power loss during config writes, load switching, thermal/electrical checks and resource accounting. Pin exact Git revision, write README with flashing, protocol, wiring, commissioning and recovery. Produce a release checklist and record Flash/SRAM headroom.

Accept: all earlier stage gates pass, no uncontrolled output transitions, repeatable release build, documented known limitations (especially single-page Flash power-loss semantics and commissioning security).

## 8. Test vectors and negative tests

- CRC16/MODBUS ASCII `123456789` -> `0x4B37`.
- Standard write single coil request (slave 1, coil 0 ON) payload before CRC: `01 05 00 00 FF 00`; verify CRC using reference implementation; expected response echoes valid request.
- Standard read coils request (slave 1, start 0, count 8) payload before CRC: `01 01 00 00 00 08`.
- Standard write multiple coils request: start 0, quantity 8, byte count 1, data mask; test 0x00, 0xFF, 0x55, 0xAA.
- Reject: bad CRC, wrong slave ID, malformed byte count, out-of-range coil/register, unsupported function, parity error, UART overrun, truncated frames, inter-byte gap >t1.5, frames longer than buffer, repeated request during TX, power failure mid-Flash-write.
- Test DE remains HIGH until actual TC; ensure no reply to broadcast. Test UID-based assignment with two unconfigured boards and collisions/slot retries.
- Test watchdog fault injection and startup output state after reset; measure WS2812 updates under continuous serial traffic.

## 9. Coding-agent execution instructions

1. Work **one stage at a time**. Before each stage, inspect actual project files and locked HAL source, list exact APIs, and identify assumptions. Never fabricate PAC register methods, peripheral pin mappings, or interrupt names.
2. Implement the smallest compiling increment, then run `cargo firmware` (and tests if host-side pure modules exist). Report compile output and binary size. Do not claim `cargo flash` or hardware validation passed unless actually run with connected hardware.
3. Preserve existing toolchain, target JSON, `no_std` and project conventions. Avoid unnecessary dependencies and dynamic allocation. Keep code and buffers small.
4. Write tests for pure CRC, parser, register mapping, configuration encode/decode, UID selection and timeout logic. Keep hardware-specific code behind small interfaces where practical.
5. Use critical sections for shared ISR/main state and document interrupt priorities; avoid atomic RMW unsupported by `riscv32ec`.
6. Do not program/erase the reserved Flash page until linker exclusion and chip-specific Flash operations have been confirmed. Require explicit approval before destructive Flash tests on devices with valuable configuration.
7. Update `docs/protocol.md`, `docs/hardware-validation.md`, README and this checklist as decisions become verified. Track deviations and unresolved questions.
8. At each stage return: files changed, architectural decisions, tests/builds run, actual outcomes, Flash/RAM usage, hardware validation still required, and next stage.

## 10. Decisions to settle before production

- Exact manufacturer-confirmed physical pinout and whether PC1 WS2812 logic levels require a shifter.
- Whether output channels should always power up OFF or apply configured startup mask (safe default OFF recommended).
- Exact Modbus framing 8E1 and RTU gap policy accepted by main controller.
- UID width and silicon uniqueness guarantee; discovery protocol byte layout and whether multi-device simultaneous commissioning is required for first release.
- Whether a single 64-byte config page with safe recovery is sufficient, or two-page atomic updates are required.
- Whether flashing tool can preserve reserved configuration across firmware updates.
- RS485 bus termination, biasing, surge protection, wiring and required failsafe behavior.
- Security/physical access controls for readdressing and configuration writes.
- Required output timeout and watchdog duration based on actual load/application safety.

## 11. References to validate against

- WCH CH32V003 datasheet (English), CH32V003 reference manual (English), manufacturer errata.
- `ch32-rs/ch32-hal` source at **locked Cargo revision**; `qingke-rt` 0.8.2 source.
- Modbus Organization: *Modbus Application Protocol Specification* and *Modbus over Serial Line Specification and Implementation Guide*.
- MaxLinear SP485EEN-L/TR datasheet, ULN2803A manufacturer datasheet, WS2812 variant datasheet.

**Do not begin by generating an entire monolithic firmware from this plan.** Begin with Stage 0 and carry forward only verified interfaces, tests, and acceptance evidence.
