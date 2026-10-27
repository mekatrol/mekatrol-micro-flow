# CH32V003 Output Expander — Firmware Implementation Status and Completion Plan

**Repository snapshot reviewed:** 2026-10-27 (uploaded `output-expander` archive).  
**Purpose:** source-aligned implementation reference, remaining work plan, and verification checklist.  
**Status:** substantial firmware implemented; **not yet a hardware-validated or production-released build**.

> This document describes the checked-in code, not the original proposed architecture. Where documentation and code disagree, the code is identified as the current implementation and the discrepancy is recorded. Source inspection is not equivalent to a successful compile or bench test. `cargo` was unavailable in the review environment; no build, host tests, flashing, or physical measurements were performed for this revision.

## 1. Hardware, toolchain and constraints

- **MCU:** WCH CH32V003F4P6, RV32EC, 16 KiB Flash, 2 KiB SRAM; firmware is Rust 2024, `#![no_std]`, fixed-capacity buffers and no heap.
- **Package:** `output-expander` v0.1.0; binary entry `src/main.rs`, reusable/testable library `src/lib.rs`.
- **Target:** `riscv32ec-unknown-none-elf.json`; `rust-toolchain.toml` pins the Rust toolchain (consult the file for the exact version).
- **Build:** `cargo firmware` expands to `cargo build --release -Z build-std=core`; `cargo flash` runs release firmware through `wlink -v flash`. `.cargo/config.toml` selects `-Tlink.x` and the custom target.
- **Dependencies:** target-specific `ch32-hal` (features `ch32v003f4p6`, `memory-x`, `rt`), `qingke-rt` 0.8.2, `qingke` 0.8.2, `panic-halt` 1.0; `critical-section` 1.2 for shared interrupt state. `Cargo.lock` pins the HAL commit `bbcabaac96f482b1cfaea111d2fc938095b9a2fd` (per `docs/hardware-validation.md`).
- **Clock:** `main.rs` selects 48 MHz HSI; WS2812 bit timing depends on this configuration.
- **Serial:** USART1, PD5 TX / PD6 RX, 19,200 bit/s, 8E1 as documented by the current protocol/board configuration. Confirm exact hardware register configuration and baud accuracy on the board.
- **Resource limits:** actual release ELF, `.text/.rodata/.data/.bss`, stack high-water and Flash/SRAM margin **have not been measured in this review**.

## 2. Actual repository layout

```text
output-expander/
├── .cargo/
│   └── config.toml              Cargo aliases, custom target, linker, wlink runner
├── Cargo.toml                   package, dependencies, release settings
├── Cargo.lock                   resolved/pinned dependencies
├── rust-toolchain.toml          Rust toolchain selection
├── rustfmt.toml                 formatting configuration
├── riscv32ec-unknown-none-elf.json  custom CH32V003 compilation target
├── README.md                    project overview / older bring-up notes
├── docs/
│   ├── FIRMWARE_IMPLENTATION_PLAN.md  this implementation reference
│   ├── BUILD.md                 build/flash instructions; behavior section outdated
│   ├── PROJECT_SETUP.md         tools, WCH-LinkE, wiring and Modbus utility
│   ├── hardware-validation.md   verified source facts and unresolved hardware gates
│   ├── protocol.md              current public Modbus protocol description
│   ├── release-checklist.md     release gate checklist (needs synchronization)
│   ├── CH32V003DS0.pdf          MCU datasheet
│   ├── CH32V003RM.pdf           MCU reference manual
│   ├── b18a_41036460-schematic.pdf  supplied board schematic
│   └── 6ba4_41036460-tds.pdf   supplied component documentation
├── src/
│   ├── lib.rs                   module exports / target-gated hardware modules
│   ├── main.rs                  hardware initialization, USART ISR, foreground loop
│   ├── board.rs                 constants, provisional pin list, active-low conversion
│   ├── output_gpio.rs           eight physical GPIO outputs (firmware target)
│   ├── outputs.rs               logical eight-bit output state
│   ├── rs485.rs                 DE direction GPIO controller (firmware target)
│   ├── serial.rs                USART configuration and blocking RS485 responses
│   ├── irq_rx.rs                interrupt-safe timestamped RX/error event queue
│   ├── irq_consumer.rs          foreground transfer of IRQ events and ready frames
│   ├── rtu_receiver.rs          bounded RTU framing and silence-gap detection
│   ├── uart.rs                  UART receive/half-duplex state utilities
│   ├── timer.rs                 hardware-independent RTU timing calculations
│   ├── monotonic.rs             free-running SysTick clock (firmware target)
│   ├── modbus.rs                Modbus framing, CRC and protocol helpers
│   ├── server.rs                complete-frame request dispatch and responses
│   ├── registers.rs             coil/input/holding data model and status flags
│   ├── commissioning.rs         UID-selected broadcast address assignment
│   ├── config.rs                versioned 64-byte Flash record codec / CRC32
│   ├── uid.rs                   96-bit silicon ID and Modbus word conversion
│   ├── watchdog.rs              IWDG access and health-gate logic
│   ├── status_led.rs            status patterns / RGB color selection
│   └── ws2812.rs                PC1 single-pixel GPIO waveform driver
└── tools/
    └── modbus_tool.py          host Modbus inspection/control utility
```

**Important:** the former draft's proposed `timer.rs` TIM2 interrupt architecture is **not** the active runtime. The firmware uses a free-running **SysTick** monotonic counter and timestamps bytes in the USART1 interrupt. The existing `uart.rs` helpers should not be confused with the actual hardware driver `serial.rs`.

## 3. Current physical interface (code-derived; PCB verification pending)

| Function | Current code mapping | Behavior / qualification |
|---|---|---|
| Outputs 1–8 | PC0, PC3, PC4, PC5, PD0, PD2, PD3, PD4 | Logical mask bits 0–7 in that order; **active LOW** in current triac/FET bring-up, GPIO HIGH = OFF. All pins initialize HIGH before any mask is applied. |
| USART1 TX/RX | PD5 / PD6 | Half-duplex Modbus RTU at 19,200 8E1. |
| RS485 DE and `/RE` | PC6 | `main.rs` instantiates a **single** PC6 direction GPIO; comments and `rs485.rs` assume DE and `/RE` electrically tied. `hardware-validation.md` says the supplied schematic has **separate** PC6 DE and PC7 `/RE`; resolve the actual board revision. |
| Push button | PC7 | Configured as input with pull-up, active LOW; exposed as discrete input 0. **Conflicts with any board that uses PC7 for `/RE`.** |
| WS2812 DIN | PC1 | Single status LED, waveform generated by `ws2812.rs`, 48 MHz clock assumption. |
| SWIO / reset | PD1 / PD7 | Preserve programming access and verify reset configuration. |

The old ULN2803A/high-active assumptions are **not valid descriptions of the current output driver**. The README mentions triac and FET board variants; do not generalize output electrical ratings or safety behavior until the exact populated board and schematic are approved. Logical `1` means output ON, but physical GPIO LOW activates a channel in the current implementation. Verify safe power-up behavior on real loads, including the interval before firmware takes control.

## 4. Runtime architecture actually implemented

1. `main.rs` initializes HAL at 48 MHz, WS2812 PC1, eight output pins HIGH/OFF, SysTick monotonic clock, PC6 RS485 direction LOW/receive, PC7 button, USART1, UID and the Modbus `Server`.
2. Firmware constructs a **default** `Config`, explicitly sets slave address **247** and startup mask **0**. It does **not** read a persisted record from Flash. `Server` receives the silicon UID via `uid::read()`.
3. `RtuReceiver` owns fixed-size receive buffers; `irq_rx` queues hardware-timestamped bytes and UART errors from the `USART1` ISR. `irq_consumer` drains events in the foreground, applies RTU timing, and publishes complete frames. Critical sections coordinate interrupt/foreground ownership.
4. Foreground processing samples PC7, passes a completed ADU to `Server::handle`, updates status and physical output mask when requested, and transmits any response through `Serial::write_response`.
5. **RX is interrupt-driven, but TX remains blocking** in `serial.rs`; the direction driver asserts DE, sends the response, waits for completion of the final transmitted character, and returns to receive mode. Do not describe this as a fully interrupt-driven TXE/TC state machine.
6. The foreground loop updates the single WS2812 when the desired color changes, drains receive events, handles frames, and feeds the independent watchdog last. A vendor reset command deliberately disables further feeds after its acknowledgement.
7. `Outcome.config_changed` is recognized, but its handler in `main.rs` is explicitly `TODO` and performs **no Flash write**. Any address assignment exists only in RAM until reset.

**Timing:** `docs/protocol.md` specifies t1.5 = 860 µs and t3.5 = 2006 µs at 19,200 8E1. The receiver uses timestamped input to enforce frame separation. Verify these thresholds, handling of long scheduling delays, queue saturation, and the actual RS485 turnaround waveform under load. Blocking TX and bit-banged LED activity require explicit stress testing.

## 5. Protocol and data model

**Framing:** Modbus RTU, 19,200 8E1, CRC-16/MODBUS (low CRC byte first), multi-byte register fields big-endian. Broadcast address `0` never receives a response. Maximum RTU ADU **128 bytes** (see `docs/protocol.md` and fixed buffers). Factory/default slave ID is **247**, not the earlier plan's `0xFF` unconfigured ID. `Config` uses `0xFF` as an internal unconfigured marker, but `main.rs` explicitly forces 247.

| Space | Zero-based addresses | Meaning |
|---|---|---|
| Coils | 0–7 | Output channels 1–8, bit 0 first; `1` = logical ON |
| Discrete inputs | 0 | PC7 push button, `1` = pressed |
| Holding registers | 0 | Slave address; normal write disabled |
| Holding registers | 1 | Startup output mask |
| Holding registers | 2 | Communications timeout in ms; zero disables |
| Holding registers | 3 | Timeout output mask |
| Holding registers | 4 | LED brightness 0–255 |
| Input registers | 0 | Firmware version, major high byte/minor low byte (`0x0001` in code) |
| Input registers | 1 | Current logical output mask |
| Input registers | 2 | Status bits: 0 configured, 1 comms timeout, 2 UART RX error, 3 configuration fault |
| Input registers | 3–8 | Full 96-bit UID, six big-endian 16-bit words in HAL UID byte order |

**Standard functions implemented in `server.rs`:** `0x01` read coils, `0x02` read discrete inputs, `0x03` read holding registers, `0x04` read input registers, `0x05` write single coil, `0x0F` write multiple coils. Unsupported or invalid requests use appropriate Modbus exceptions or silence for invalid CRC/address/broadcast, as applicable. **`0x06` write single register is not enabled** pending validated persistent commit semantics.

**Vendor functions:**

- `0x41` commissioning: broadcast ADU with payload `03 | UID[12] | NEW_ID` (subcommand `0x03`), followed by Modbus CRC. Only a board whose complete UID matches accepts the assignment; it does not reply to the broadcast. Assignment can target an already configured board. **Persistence is not implemented in the runtime**, despite the `config_changed` flag. Read UID back from the proposed new address and confirm after reboot once Flash persistence exists. No multi-board discovery or unsolicited replies.
- `0x42` watchdog reset: addressed ADU `SLAVE | 42 | CRC`, no payload. Acknowledge, then stop feeding the IWDG; LED stays solid red until reset. This command is **unauthenticated**; use only on trusted buses. Broadcast reset requests are ignored.

The authoritative wire-format summary is `docs/protocol.md`; keep it synchronized with `server.rs`, `registers.rs`, and `commissioning.rs`.

## 6. Persistent configuration: implemented codec, missing runtime storage

`src/config.rs` defines a **64-byte** version-1 record with `MEK1` magic (`0x4D454B31`), fixed fields, erased-value reserved bytes and CRC32 over the record excluding its final four CRC bytes. Settings include slave address, startup mask, communications timeout, timeout mask and LED brightness. The record codec supports validation and corruption rejection and has host-side tests.

**Not implemented/verified:** reserved final Flash page in the linker memory map; target-specific Flash erase/program/read/verify integration; loading the record at startup; committing `config_changed`; preserving the page through `wlink` flashing; atomicity/recovery on interrupted writes; endurance limits. `docs/hardware-validation.md` explicitly states HAL-generated `memory.x` still covers all 16 KiB. **Do not program configuration Flash until linker exclusion and erase behavior are demonstrated.**

A single 64-byte page is not inherently power-loss atomic. Decide whether losing the prior configuration on an interrupted erase is acceptable, or whether a redundant journal/two-page scheme and associated linker reservation are required. Do not advertise persistent commissioning until power-cycle tests pass.

## 7. Watchdog, status LED and operational safety

- **IWDG:** `watchdog.rs` uses CH32V003 IWDG registers directly; nominal timeout 2,000 ms (LSI oscillator tolerance applies). `main.rs` starts it after initialization and feeds in the foreground loop. A valid `0x42` request prevents further feeding after response transmission. The `HealthGate` helper exists, but **the main loop currently feeds based on `watchdog_feed_enabled`, not a comprehensive RX/TX/foreground health-gate assessment**. Reset-cause reporting is not integrated.
- **LED:** `status_led.rs` provides color/pattern logic; `ws2812.rs` drives PC1. Runtime displays a short blue command pulse, green normal heartbeat, red error indication, and solid red while awaiting watchdog reset. `STATUS_LED_BRIGHTNESS` is hardcoded to **32** in `main.rs`; the persisted/configured brightness field is **not applied** to the runtime LED. Confirm timing and voltage margin with a scope.
- **Output timeout:** config and status flags define communications timeout concepts, but the shown `main.rs` loop does **not** evaluate elapsed time since valid communications or apply `timeout_outputs`. Treat fail-safe communications timeout as **pending runtime integration**.
- **Startup:** current firmware forces startup mask zero and sets all eight GPIOs inactive before applying the logical mask. The configuration's startup mask is not loaded from Flash. Check hardware-level off state before GPIO initialization and after brownout/watchdog reset.
- **Fault behavior:** distinguish UART receive errors, invalid Modbus CRC, application exceptions, config storage faults, and true communications timeout; confirm that clearing status flags on a later valid request matches the desired diagnostics policy.

## 8. Implementation status by subsystem

| Area | Code status | Remaining acceptance |
|---|---|---|
| Project/toolchain and HAL | Configured in repository | Fresh release build, size/map, programmer and PCB validation |
| GPIO output mask and polarity | Implemented | Verify all eight pins, physical channels, glitch-free reset |
| USART1 receive ISR and event queue | Implemented | RX stress, overflow/error recovery, interrupt latency |
| RTU timing, CRC, parser and server | Implemented | Interoperability, malformed traffic, timing and soak |
| RS485 transmission and DE control | Blocking TX implemented | Confirm DE wiring, TC timing, contention and turnaround |
| Coils/discrete/holding/input read map | Implemented | Verify all address boundaries and device-side behavior |
| UID exposure | Implemented | Check UID stability/uniqueness on sample boards |
| UID-selected commissioning | RAM assignment implemented | Flash commit, reboot verification, isolated-device test |
| Flash record codec | Implemented and unit-tested in source | Linker reservation, erase/write/verify, power-fail tests |
| Runtime config persistence | **Not implemented** | Load/save integration and fault policy |
| Comms timeout and fallback mask | **Not wired into main loop** | Timeout application and recovery tests |
| IWDG and deliberate reset command | Implemented in source | Hardware fault injection, health gating, reset cause |
| WS2812 status | Implemented in source | LED timing/load tests, configurable brightness |
| Fully interrupt-driven TX | **Not implemented** | Optional design improvement if blocking TX fails load testing |
| Production release | **Not accepted** | Bench, soak, power cycling, resource and release evidence |

## 9. Remaining work in recommended order

### P0 — Resolve hardware truth and obtain a reproducible baseline

1. Confirm the exact PCB revision against the supplied schematic, especially **PC7 button versus `/RE`**, PC6 direction wiring, eight output routing, active-low output stage, WS2812 supply/logic level, and reset/power-on pull states.
2. Run `cargo test` on host and `cargo firmware` with the pinned toolchain. Capture warnings, failing tests, release ELF size, section sizes, linker map and stack budget. Do not claim these checks passed based on source inspection.
3. Bench-test all eight channels, USART 8E1, RX IRQ recovery, DE/TC transitions, and LED waveform; record instruments and measurements in `docs/hardware-validation.md`.

### P1 — Finish safe runtime configuration and Flash persistence

1. Prove how to override or replace the HAL-generated Flash memory layout so the reserved page(s) cannot be linked into firmware. Check `wlink` erase strategy.
2. Implement Flash load on boot, blank/corrupt record fallback and explicit fault indication. Decide whether factory address 247 is intended and document collisions with multiple factory devices.
3. Implement bounded erase/program/verify on a real board; commit only after validating the requested change. Define error response/rollback semantics, power-loss behavior and wear limits.
4. Apply stored startup mask and LED brightness only under an approved safety policy; preserve the initial OFF GPIO setup.
5. Reboot and power-cycle after `0x41` assignment to prove the address persists. Only then consider enabling standard holding-register writes (`0x06`) with specified commit behavior.

### P2 — Complete runtime safety and diagnostic behavior

1. Track the last valid command/heartbeat using SysTick and enforce configured `comms_timeout_ms` and `timeout_outputs`; define which requests refresh the timeout and when normal operation resumes.
2. Integrate meaningful `HealthGate` conditions if needed so an ISR/queue deadlock or stalled protocol worker cannot be hidden by a healthy foreground feed loop.
3. Capture reset cause if supported and document what survives reset. Fault-inject stalled foreground, continuous noisy UART, and watchdog-triggered reset.
4. Review LED status priority and whether the configuration brightness field should drive the physical LED.

### P3 — Protocol and electrical robustness

1. Verify 0x01/02/03/04/05/0F and 0x41/42 with an independent Modbus master; exercise wrong address, CRC errors, partial frames, t1.5/t3.5 boundaries, oversized ADUs, queue overflow, broadcasts, malformed counts and output-mask transitions.
2. Scope RS485 DE against the final stop bit; confirm receiver re-enable and absence of bus contention on a multidrop network.
3. Test continuous traffic while WS2812 updates and blocking responses run. Convert TX to an interrupt-driven state machine **only if required by measured latency/throughput**; do not claim this work already exists.
4. Define isolated-device commissioning instructions and duplicate-address recovery. Multi-device discovery remains out of scope unless a collision-safe master/device protocol is separately specified and tested.

### P4 — Documentation and release

1. Correct stale **`docs/BUILD.md`**: it still describes a newline-terminated 8N1 RS485 echo firmware, not the current 8E1 Modbus runtime. Reconcile any similar bring-up wording in `README.md` and `docs/PROJECT_SETUP.md`.
2. Update `docs/hardware-validation.md` (it currently says IWDG and WS2812 bindings are not enabled, while `main.rs` starts/drives both), `docs/release-checklist.md` (its interrupt/TIM2 item no longer matches architecture), and `docs/protocol.md` whenever behavior changes.
3. Record full-chip erase/config-preservation procedure, pinout by PCB revision, UID and commissioning workflow, safe output behavior, error handling, resource headroom and known limitations.
4. Complete power-cycle, Flash-interruption, multidrop soak, load-switching, reset, thermal and electrical tests before production approval.

## 10. Verification commands and evidence template

From the project root:

```sh
cargo test
cargo firmware
# Inspect the release ELF, section sizes, and linker map using your installed
# RISC-V/LLVM binutils; record the exact commands and measured byte counts.
# Only with a connected, approved test board and an understood erase policy:
cargo flash
python3 tools/modbus_tool.py --help
```

For each accepted milestone record: **commit hash; PCB revision; toolchain/HAL versions; commands run; pass/fail output; Flash and SRAM bytes; instrument captures; power-cycle results; unresolved issues; and reviewer sign-off.** Hardware tests must be labeled *not run* until actually performed.

## 11. Immediate inconsistencies and decisions requiring resolution

1. **PC7 is both claimed as `/RE` in `hardware-validation.md` and used as a button in `main.rs`.** The actual schematic and PCB must decide which is correct; a software comment cannot resolve an electrical conflict.
2. **Flash persistence is absent**, even though the config record, server change flag and UID commissioning parser exist. Readdressing is temporary today.
3. **Comms timeout and timeout output mask are defined but not enforced** in the main loop.
4. **Watchdog health-gate helper is not wired into the feed decision**; reset-cause reporting remains pending.
5. **Configured LED brightness is not used**; runtime uses a fixed constant.
6. **`docs/BUILD.md` describes obsolete echo/8N1 behavior**, and `docs/hardware-validation.md`/`release-checklist.md` contain stale implementation statements.
7. **Original ULN2803A/active-HIGH assumptions conflict with current active-LOW triac/FET driver**; do not use the original electrical description as a wiring specification.
8. **Factory address 247 is an active slave address**, not a collision-free unconfigured discovery mode. Isolate factory boards during initial commissioning.
9. **Blocking TX is intentional current code**, not a completed TX interrupt state machine. SysTick, not TIM2, supplies the runtime timestamps.

## 12. References

Use the checked-in `docs/CH32V003DS0.pdf`, `docs/CH32V003RM.pdf`, schematic and component document, pinned `ch32-hal` sources, Modbus Application Protocol and Modbus over Serial Line specifications. `docs/protocol.md` defines current wire behavior; `src/main.rs`, `src/server.rs`, `src/config.rs` and the hardware modules define current implemented behavior. Resolve conflicts by inspecting the target PCB and testing the firmware, then update all documents together.
