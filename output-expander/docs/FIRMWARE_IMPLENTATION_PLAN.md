# CH32V003 Output Expander — Firmware Implementation Plan

## 1. Purpose and scope

Build an open-source, eight-channel, active-low output expander for a RS-485 automation network. The device is controlled by the project's own hardware or a host gateway, with a documented client suitable for integration into automation systems. The target is the CH32V003F4P6 (16 KiB program Flash, 2 KiB SRAM), subject to verified resource and pin budgets before PCB release.

This document describes the **current repository baseline** and **work required for the target design**. All proposed hardware mappings require schematic and HAL pin-mux verification.

## 2. Current repository baseline

The supplied firmware currently provides eight GPIO outputs, an active-low push button, a WS2812 status LED, a half-duplex USART1/RS-485 driver, SysTick timing, an independent watchdog, and interrupt-driven UART reception with blocking transmission. Its request processing is Modbus-specific, including RTU silence timing, register-bank handling, Modbus CRC, and commissioning. The current build enables the `qingke-rt` `highcode` feature and places the software-driven WS2812 routine, interrupt vectors, traps and interrupt handlers in SRAM. The supplied `main.rs` references internal-Flash configuration storage and contains a diagnostic reference to Flash erase/write functions. The target architecture below is **not yet implemented**.

### Current pins in supplied source

| Function | Current assignment |
|---|---|
| Outputs 1–8, active LOW | PC0, PC3, PC4, PC5, PD0, PD2, PD3, PD4 |
| WS2812 status LED | PC1 |
| RS-485 USART1 TX / RX | PD5 / PD6 |
| RS-485 DE and /RE, electrically tied | PC6 |
| Push button, active LOW | PC7 |
| SWIO / reset | PD1 / PD7 (preserve access) |

## 3. Proposed production pin assignment

The CH32V003 hardware I²C interface is planned for PC1 (SDA) and PC2 (SCL), subject to confirmation against the exact CH32V003F4P6 datasheet, alternate-function remapping and the pinned `ch32-hal` implementation. Relocate the WS2812 signal to PA1, using the internal 48 MHz HSI clock so the external oscillator function is unnecessary. This keeps the existing eight output assignments.

| Function | Proposed MCU signal | Notes |
|---|---|---|
| Output 1 | PC0 | Active LOW; safe OFF = HIGH |
| Output 2 | PC3 | Active LOW |
| Output 3 | PC4 | Active LOW |
| Output 4 | PC5 | Active LOW |
| Output 5 | PD0 | Active LOW; verify oscillator/boot implications |
| Output 6 | PD2 | Active LOW |
| Output 7 | PD3 | Active LOW |
| Output 8 | PD4 | Active LOW |
| EEPROM SDA | PC1 | I²C open-drain, pull-up |
| EEPROM SCL | PC2 | I²C open-drain, pull-up |
| WS2812 DIN | PA1 | Recalibrate/verify GPIO timing and alternate-function availability |
| USART1 TX | PD5 | RS-485 |
| USART1 RX | PD6 | RS-485 |
| RS-485 DE and /RE | PC6 | Tie DE and active-low /RE; LOW = receive |
| Commissioning button | PC7 | Active LOW with pull-up |
| SWIO | PD1 | Do not allocate to application |
| NRST | PD7 | Preserve reset/debug access |

**Hardware gate:** Confirm PA1 is bonded out on the selected package, that the proposed I²C pins and USART mapping can operate simultaneously, that no oscillator/boot/alternate-function conflict exists, and that the WS2812 signal is electrically valid. If the MCU/package cannot support this mapping, revise the design before PCB layout; do not silently sacrifice programming or reset access.

## 4. Persistent configuration: external EEPROM

Use the ordered **SOT-23-5 24C02-class I²C EEPROM**, nominal capacity 2 Kbit = 256 bytes. The exact manufacturer's datasheet, ordering code, pinout, supply range, I²C address, maximum bus frequency, write-page size, write-cycle time and endurance are **mandatory design inputs**. Do not assume a generic 8-pin 24C02 footprint or address pins exist on the five-pin device.

### Electrical design

- Connect SDA and SCL to the selected hardware-I²C pins with open-drain operation.
- Start with 4.7 kΩ pull-ups to the **same 3.3 V rail** as the MCU, then validate rise times for actual wiring and bus capacitance. The EEPROM bus is local to the PCB; it is not the external RS-485 cable.
- Place a 100 nF ceramic bypass capacitor directly at the EEPROM supply pins.
- Use the exact manufacturer SOT-23-5 footprint and pin numbering. Confirm any write-protect/address behavior from the specific datasheet.
- Use ACK polling or bounded write-cycle waits; do not assume data is committed immediately after I²C STOP.
- Write only on explicit persistent configuration changes, not on every output command, heartbeat or main-loop iteration.

### Record format and recovery

Retain a fixed **64-byte versioned configuration record**, with magic, schema version, address, startup output mask, communications timeout, timeout output mask, LED brightness, sequence number and CRC-32. The existing `config.rs` codec can be adapted, but its field layout and reserved bytes must be reviewed before adding the sequence number. CRC-32 here protects stored records; wire frames use CRC-16.

| EEPROM byte range | Allocation |
|---|---|
| `0x00..0x3F` | Configuration slot A (64 bytes) |
| `0x40..0x7F` | Configuration slot B (64 bytes) |
| `0x80..0xFF` | Reserved for future settings/schema growth |

At startup, read both slots, validate magic/version/field bounds/CRC, and select the valid record with the newest sequence number (including defined wraparound handling). If neither is valid, use safe factory defaults and flag configuration as uncommissioned or invalid as appropriate. On save, write the **inactive** slot in EEPROM-page-sized chunks, poll completion, read back and validate the complete record before making it the preferred slot. Never erase or overwrite the sole valid slot first. Explicitly specify behavior for write failure, power loss, blank EEPROM and schema upgrades. Do not treat CRC as authentication.

The device should not require a custom `memory.x`, Flash-reserved pages, RAM-resident Flash writer or Flash erase/program operations for configuration. Keep standard linker/build infrastructure only if needed independently by the HAL/toolchain. Verify whether `build.rs` and `memory.x` are needed for any other purpose before removing them.

## 5. Proprietary RS-485 protocol

Implement the versioned protocol specified in `docs/protocol.md`. Use 19,200 bit/s **8N1** as an initial target, configurable later only through a defined migration process. Use **CRC-16/CCITT-FALSE** (polynomial `0x1021`, init `0xFFFF`, no reflection, xorout `0x0000`) over all decoded frame bytes preceding the CRC. Use COBS encoding and `0x00` frame delimiters to avoid dependence on Modbus inter-character timing. The exact packet layout, command identifiers, sequence rules, addressing and error handling are defined in the companion protocol document.

Use fixed-capacity receive and transmit buffers and no dynamic allocation. The parser must reject oversize, malformed, bad-CRC and unsupported-version frames without applying outputs. No device may reply to a broadcast. The master arbitrates the half-duplex bus; only addressed responses may drive it. Retain the RS-485 direction control and wait for USART transmission-complete before deasserting DE.

Commissioning uses the MCU's full 96-bit UID to target one device. Uncommissioned units must not spontaneously transmit on a shared bus. Factory discovery and address assignment should be performed with one uncommissioned unit connected at a time unless a separately validated collision-free discovery mechanism is introduced. Reserve `0x00` for broadcast, `0x01..0xFE` for assigned addresses, and `0xFF` for uncommissioned/internal state (never a normal addressed response). Address assignments must be persisted and verified before reporting success.

## 6. Device behavior

- **Outputs:** Eight channels; mask bit 0 controls output 1; logical 1 = ON; physical GPIO LOW = ON. Set every GPIO HIGH/OFF before applying any configured startup mask. Confirm external pull states and load safety during reset/boot.
- **Status LED:** WS2812 on proposed PA1. Green normal heartbeat, brief activity indication, red fault indication, and deliberate-reset indication; implement priority and brightness from configuration. Service LED updates from the foreground main loop as a low-priority operation, never from an interrupt. Execute the driver from Flash without `#[highcode]`; verify its waveform timing at 48 MHz and replace or redesign the driver if reliable timing would otherwise require code execution from SRAM.
- **Button:** Active-low commissioning/service input on PC7. Define short/long-press behavior and debounce before release; do not allow accidental address changes.
- **Communication timeout:** Track valid addressed control/heartbeat traffic using SysTick. On timeout, apply configured fallback output mask and report a latched or current fault according to the protocol. Zero timeout disables this feature.
- **Watchdog:** Feed only from the lowest-priority point of the foreground main loop, alongside low-priority status-LED service, and only after the loop has made meaningful progress. No interrupt handler, timer callback or higher-priority execution path may feed the watchdog. If foreground processing stalls or is starved by interrupts, the watchdog must expire and reset the device. Define behavior when I²C is stuck, UART is noisy, or TX stalls; every blocking wait must be bounded. Deliberate reset is acknowledged before the watchdog is allowed to expire or an explicit reset is requested.
- **Storage faults:** A failed EEPROM read or commit must be observable in status. Define whether an attempted address change takes effect immediately or only after successful commit; prefer **commit first, activate second**.

## 7. Firmware module structure

| Module | Target responsibility |
|---|---|
| `board.rs` | Validated pin map, output polarity, baud and fixed capacities |
| `main.rs` | HAL setup, safe GPIO startup, EEPROM load, protocol loop, lowest-priority LED service and watchdog feed |
| `output_gpio.rs`, `outputs.rs` | Eight-channel logical/physical output control |
| `serial.rs`, `rs485.rs` | USART1 and half-duplex transceiver direction |
| `irq_rx.rs` | Minimal interrupt-safe receive transport and UART error reporting |
| `protocol.rs` | COBS, CRC-16, header validation and packet encoding/decoding |
| `dispatcher.rs` | Command execution and response generation, independent of framing |
| `config.rs` | Versioned 64-byte record codec and validation |
| `eeprom.rs` | I²C EEPROM byte/page operations, ACK polling and timeouts |
| `config_store.rs` | Dual-slot selection, sequence handling, verified commits |
| `uid.rs` | MCU UID acquisition and commissioning identity |
| `monotonic.rs`, `watchdog.rs` | Timing and fault recovery |
| `status_led.rs`, `ws2812.rs` | Status display |
| `tools/device_tool.py` | Python host CLI, discovery/commissioning/control |

Remove Modbus-specific modules, register abstractions, RTU timing and tooling **when their replacements are implemented and tested**. Preserve reusable GPIO, serial, watchdog and LED logic where appropriate. The current source does not yet implement this target structure.

## 8. Resource requirements

The target has only 2 KiB SRAM. Do not enable the `qingke-rt` `highcode` feature or use `#[highcode]`; interrupt vectors, traps, handlers, the WS2812 driver and application code must execute from Flash. The release ELF must contain no allocated `.highcode` section in SRAM. Remove the current `highcode` feature and annotation, then validate interrupt operation and the WS2812 waveform on hardware. If any timing-sensitive feature cannot operate reliably from Flash, redesign that feature rather than consuming SRAM with executable code.

Measure `.data`, `.bss`, the `main` stack frame, nested calls and interrupt stack usage on every release build. A successful link is not proof of stack safety. Eliminate the RAM-resident internal-Flash writer and reduce duplicated RTU/event/request/response buffers. Define a maximum decoded packet length of **32 bytes** and allocate bounded COBS wire buffers; revise only with a documented command requirement. Use no heap.

Treat SRAM headroom as a hardware-selection gate. If the measured worst-case stack and planned feature growth cannot fit safely, select a larger MCU before PCB fabrication rather than hiding the problem by moving stack objects to statics.

## 9. Implementation sequence and acceptance criteria

### P0 — Confirm hardware and clean baseline

1. Verify the exact SOT-23-5 EEPROM datasheet and pinout, MCU package pinout, I²C alternate-function mapping and PA1 WS2812 availability.
2. Remove the `qingke-rt` `highcode` feature and all `#[highcode]` annotations. Confirm the release ELF executes vectors, traps, interrupt handlers and the WS2812 driver from Flash and allocates no `.highcode` section in SRAM.
3. Revert internal-Flash persistence experiments. Confirm whether custom `build.rs`/`memory.x` are required for unrelated build reasons; otherwise use the standard HAL memory layout.
4. Record a clean host-test and release-build baseline, Flash/SRAM section sizes and stack analysis.
5. Confirm outputs default OFF at reset and during programming/debug access.

### P1 — Implement EEPROM configuration

1. Implement a bounded hardware-I²C EEPROM driver with host-testable transaction logic.
2. Implement and test the dual-slot versioned record format, sequence selection and CRC-32.
3. Integrate startup loading, safe defaults, commit verification, error reporting and power-loss recovery.
4. Bench-test blank/corrupt EEPROM, interrupted writes, repeated commits and power cycling.

### P2 — Implement the proprietary protocol

1. Freeze protocol v1 framing, CRC-16, command IDs, address space, sequence/response behavior and error codes.
2. Implement bounded COBS framing and CRC checks with host-side malformed-frame/fuzz tests.
3. Replace Modbus server/register/RTU logic with a small dispatcher and one receive/response buffer strategy.
4. Implement UID-targeted commissioning, persistent address assignment, status, output control, configuration and reset commands.
5. Build a Python reference client and command-line utility; document gateway integration for Home Assistant.

### P3 — Safety and system behavior

1. Implement communications timeout/fallback outputs and recovery rules.
2. Verify RS-485 DE/TC timing, multidrop traffic, noise, UART overrun and watchdog behavior.
3. Verify the Flash-resident LED driver's timing and priorities on the final pin and electrical design. Confirm LED service occurs only in the foreground loop and cannot delay watchdog feeding beyond its allowed budget.
4. Verify that only the lowest-priority foreground path feeds the watchdog. Deliberately stall the foreground loop and create sustained interrupt load; in both cases the watchdog must expire rather than being kept alive by an interrupt or callback.
5. Test power-up, brownout, watchdog reset, EEPROM bus faults and output fail-safe behavior.

### P4 — PCB and release gates

1. Freeze pin map, power supply, EEPROM footprint, pull-ups, RS-485 termination/biasing, programming header and safe output circuitry.
2. Verify the release ELF has no SRAM-allocated `.highcode`, fits Flash and SRAM with measured worst-case stack margin and planned feature reserve.
3. Complete Python interoperability, multi-device soak, electrical-noise and power-cycle tests.
4. Publish the protocol, board schematic, firmware build instructions, EEPROM format and commissioning/recovery procedure under the project's open-source license.

## 10. Build and verification

From the firmware project root:

```sh
cargo test --lib --target x86_64-unknown-linux-gnu
cargo firmware

OBJDUMP="$(rustc +nightly --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin/llvm-objdump"
"$OBJDUMP" -h target/riscv32ec-unknown-none-elf/release/output-expander
```

Do not flash unvalidated EEPROM pin assignments or enable persistent writes until the exact part datasheet and hardware have been checked. Maintain a release checklist covering GPIO safety, absence of SRAM-resident executable code, RAM/stack margin, CRC rejection, EEPROM recovery, multidrop operation, Flash-resident WS2812 timing, foreground-only watchdog feeding and watchdog recovery.
