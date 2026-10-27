# CH32V003 Output Expander — Firmware Implementation Plan

## 1. Purpose and scope

Build an open-source, eight-channel, active-low output expander for a RS-485 automation network. The device is controlled by the project's own hardware or a host gateway, with a documented client suitable for integration into automation systems. The target is the CH32V003F4P6 (16 KiB program Flash, 2 KiB SRAM), subject to verified resource and pin budgets before PCB release.

This document describes the **current repository baseline** and **work required for the target design**. All proposed hardware mappings require schematic and HAL pin-mux verification.

## 2. Current repository baseline

The supplied firmware currently provides eight GPIO outputs, an active-low push button, a WS2812 status LED, a half-duplex USART1/RS-485 driver, SysTick timing, an independent watchdog, interrupt-driven UART reception through a fixed-capacity ring buffer, COBS framing, CRC-16/CCITT-FALSE and a protocol-independent command dispatcher. Modbus register, server and RTU-silence modules have been removed. The firmware does not enable the `qingke-rt` `highcode` feature and has no `.highcode` section; `qingke-rt` remains a transitive dependency of the required `ch32-hal` `rt` feature. The existing WS2812 implementation is not part of the target architecture and must be removed when the mandatory timer/PWM/DMA driver below is integrated. External EEPROM persistence, production pin relocation and some target behavior below remain to be implemented and hardware-validated.

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
| WS2812 DIN | PA1 | TIM1_CH2 PWM output; pinned HAL exposes remap group 0 or 2. Verify package bonding, remap choice and electrical timing. |
| USART1 TX | PD5 | RS-485 |
| USART1 RX | PD6 | RS-485 |
| RS-485 DE and /RE | PC6 | Tie DE and active-low /RE; LOW = receive |
| Commissioning button | PC7 | Active LOW with pull-up |
| SWIO | PD1 | Do not allocate to application |
| NRST | PD7 | Preserve reset/debug access |

**Hardware gate:** Confirm PA1 is bonded out on the selected package, that TIM1_CH2 can drive it with the selected remap, that the proposed I²C pins and USART mapping can operate simultaneously, that no oscillator/boot/alternate-function conflict exists, and that the WS2812 signal is electrically valid. If the MCU/package cannot support this mapping, revise the design before PCB layout; do not silently sacrifice programming or reset access. The current PC1 board may use TIM2_CH4 remap group 1 for bring-up; do not confuse that temporary mapping with the proposed PA1 production mapping.

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
- **Status LED:** WS2812 on proposed PA1. Green normal heartbeat, brief activity indication, red fault indication, and deliberate-reset indication; implement priority and brightness from configuration. The foreground main loop selects the desired color at low priority, but timer PWM and DMA generate the timing-sensitive waveform. Do not bit-bang the waveform from either Flash or SRAM, and do not service every WS2812 bit or edge with a CPU interrupt. The only permitted LED interrupt is a bounded transfer-completion interrupt which stops the PWM output, forces the pin LOW and advances driver state. Detailed requirements are below.
- **Button:** Active-low commissioning/service input on PC7. Define short/long-press behavior and debounce before release; do not allow accidental address changes.
- **Communication timeout:** Track valid addressed control/heartbeat traffic using SysTick. On timeout, apply configured fallback output mask and report a latched or current fault according to the protocol. Zero timeout disables this feature.
- **Watchdog:** Feed only from the lowest-priority point of the foreground main loop, alongside low-priority status-LED service, and only after the loop has made meaningful progress. No interrupt handler, timer callback or higher-priority execution path may feed the watchdog. If foreground processing stalls or is starved by interrupts, the watchdog must expire and reset the device. Define behavior when I²C is stuck, UART is noisy, or TX stalls; every blocking wait must be bounded. Deliberate reset is acknowledged before the watchdog is allowed to expire or an explicit reset is requested.
- **Storage faults:** A failed EEPROM read or commit must be observable in status. Define whether an attempted address change takes effect immediately or only after successful commit; prefer **commit first, activate second**.

### WS2812 timer, PWM and DMA architecture (required)

The CH32V003F4P6 exposes two hardware timers which are otherwise unused by the current firmware: advanced-control `TIM1` and general-purpose `TIM2`. Keep SysTick as the existing free-running monotonic time source; do not repurpose or reset SysTick for LED generation because UART diagnostics, status timing and bounded RS-485 delays share it.

Use the timer channel appropriate to the physical board:

| Board stage | LED pin | Timer output | HAL remap | Channel DMA |
|---|---|---|---:|---|
| Current board/bring-up | PC1 | TIM2_CH4 | 1 | DMA1_CH7 |
| Proposed production board | PA1 | TIM1_CH2 | 0 or 2, select and document one | DMA1_CH3 |

These mappings are present in the pinned `ch32-hal` generated metadata and its `SimplePwm` waveform support, but they remain subject to schematic, package and bench verification. Confirm that the selected whole-timer remap does not move another timer function onto a pin used elsewhere. Reserve the selected timer channel and DMA channel in `board.rs` so later USART, I²C or other DMA work cannot allocate the same resource accidentally.

At a 48 MHz timer clock, configure PWM for the WS2812 nominal 800 kHz bit rate:

| Quantity | Timer clocks | Nominal duration |
|---|---:|---:|
| Complete bit period | 60 | 1.250 µs |
| Logical-zero high time | 18 | 0.375 µs |
| Logical-one high time | 36 | 0.750 µs |

Verify the timer's auto-reload semantics when programming the registers: a 60-clock period commonly requires an ARR value of 59, while the compare values remain the number of clocks the output is high. Use PWM polarity that idles and resets LOW. Enable compare preload so a DMA write changes the following PWM period at a defined update boundary rather than altering a pulse already in progress.

Convert each requested `Rgb` color to the WS2812's 24-bit GRB, most-significant-bit-first order. Build a 24-entry `u16` duty array in which each entry is 18 for a zero bit or 36 for a one bit. The duty array occupies 48 bytes. A second 24-entry buffer is permitted if double buffering materially simplifies ownership, but document the additional 48-byte SRAM cost and retain latest-color-wins behavior. Do not allocate these buffers on a heap.

Use a timer update event to request one DMA transfer per bit period. DMA writes the next duty value to the selected capture/compare register while the timer produces the waveform in hardware. The CPU must not receive an interrupt every 1.25 µs: an 800 kHz per-bit interrupt, or up to 1.6 MHz when interrupting on both edges, provides only 60 or approximately 30 CPU clocks between events at 48 MHz and would create unacceptable latency, Flash-fetch sensitivity and USART starvation. Timer PWM plus DMA makes high and low durations independent of foreground execution, Flash wait states and ordinary interrupt latency.

Define a small non-blocking driver state machine, for example:

1. `Idle`: PWM output is disabled and the pin is held LOW. A requested color is converted into the duty buffer and starts a transfer.
2. `Transmitting`: timer and DMA own the pin for exactly 24 PWM periods. Main may replace one pending color but must not modify the DMA-owned buffer.
3. `ResetLow`: DMA completion disables the channel/timer and forces the pin LOW. Hold LOW for at least the selected WS2812 latch interval; retain the current conservative 300 µs interval unless hardware validation justifies a smaller value above the LED's specified minimum.
4. After the reset deadline, return to `Idle` or immediately start the latest pending color.

The public foreground API should express intent rather than waveform mechanics, for example `status_led.set_color(desired_color)` followed by a bounded `status_led.service(now_ticks)` call. If idle, service starts a transfer. If a transfer or reset interval is active, a new color replaces the pending color so stale intermediate heartbeat states are not transmitted. Repeated requests for the already displayed or pending color should do nothing. LED service must never block the main loop for the approximately 30 µs data waveform or the reset interval.

Use either a DMA transfer-complete interrupt or a provably bounded foreground completion check. If an interrupt is used, it must only acknowledge DMA status, disable PWM/DMA, force LOW, record the reset-start timestamp/state and return. It must not calculate colors, refill buffers, update status policy, transmit RS-485, access EEPROM or feed the watchdog. The main loop remains the sole owner of color policy and the only path that feeds the watchdog.

Pay particular attention to the first and last PWM periods. Preload the first duty value before enabling output so no spurious leading pulse is generated. After the twenty-fourth bit, prevent the timer from repeating the final duty value; stop PWM and establish LOW before another period begins. Verify the exact DMA transfer count, update-event ordering, compare preload behavior and completion flag timing against the pinned HAL and on a logic analyzer rather than assuming desktop tests prove waveform correctness.

Timer/DMA acceptance measurements on both the current and production pin mappings must include:

- all-zero, all-one and alternating-bit patterns with measured 18/36/60-clock-equivalent timing;
- correct GRB and most-significant-bit-first order for red, green, blue, white and off;
- exactly 24 data periods followed by a LOW reset interval, with no repeated final bit or startup glitch;
- color requests arriving while `Transmitting` and `ResetLow`, proving latest-color-wins behavior without buffer races;
- simultaneous sustained USART receive traffic, proving no UART overrun and no LED pulse distortion;
- foreground stalls and deliberately delayed completion servicing, proving the hardware waveform remains valid and the watchdog is not fed by LED ISR activity;
- release builds executing vectors, handlers and application routines from Flash with no `.highcode` section and no executable `.data` workaround remaining.

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
| `status_led.rs` | Foreground color policy, priority, brightness and latest requested state |
| `ws2812.rs` | Non-blocking timer/PWM/DMA waveform driver, fixed duty buffer, transfer/reset state machine and minimal completion handling |
| `tools/device_tool.py` | Python host CLI, discovery/commissioning/control |

Modbus-specific modules, register abstractions, RTU timing and tooling have been removed after their protocol replacements were implemented and host-tested. Preserve reusable GPIO, serial and watchdog logic while completing EEPROM persistence, communications timeout behavior and the timer/PWM/DMA LED replacement described here.

## 8. Resource requirements

The target has only 2 KiB SRAM. Do not enable the `qingke-rt` `highcode` feature or use `#[highcode]`; interrupt vectors, traps, handlers, the WS2812 control/state code and other application code must execute from Flash. The release ELF must contain no allocated `.highcode` section and no executable routine in `.data`. WS2812 waveform generation is exclusively the responsibility of timer/PWM/DMA hardware; CPU delay-loop waveform generation from either Flash or SRAM is not an alternative architecture.

Budget at least 48 bytes of static SRAM for one 24-entry `u16` WS2812 duty buffer plus explicit driver state. If double buffering is selected, budget 96 bytes for duty arrays and justify why a single DMA-owned buffer plus one pending RGB value is insufficient. Include DMA/HAL state introduced by enabling the selected channel in the measured `.data` and `.bss` totals; source-level array sizes alone are not the complete cost.

Measure `.data`, `.bss`, the `main` stack frame, nested calls and interrupt stack usage on every release build. A successful link is not proof of stack safety. Eliminate RAM-resident internal-Flash and WS2812 routines and avoid duplicated event/request/response buffers. Define a maximum decoded packet length of **32 bytes** and allocate bounded COBS wire buffers; revise only with a documented command requirement. Use no heap.

Treat SRAM headroom as a hardware-selection gate. If the measured worst-case stack and planned feature growth cannot fit safely, select a larger MCU before PCB fabrication rather than hiding the problem by moving stack objects to statics.

## 9. Implementation sequence and acceptance criteria

### P0 — Confirm hardware and clean baseline

1. Verify the exact SOT-23-5 EEPROM datasheet and pinout, MCU package pinout, I²C alternate-function mapping and PA1 WS2812 availability.
2. Record the current transition baseline accurately: the release ELF must have no `.highcode` section and vectors, traps and interrupt handlers must execute from Flash. The direct `qingke-rt` dependency, its `highcode` feature and all `#[highcode]` annotations remain removed; the runtime remains transitively required by `ch32-hal`. Treat any legacy CPU-driven WS2812 waveform routine only as code to remove at P3, not as a release requirement to preserve, relocate or recalibrate.
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
3. Implement the required timer/PWM/DMA LED state machine and remove the legacy CPU-driven waveform implementation. Verify PC1/TIM2_CH4 on current hardware and PA1/TIM1_CH2 on the production mapping as applicable. Measure bit periods, zero/one duty, first/final pulse behavior, reset LOW interval, GRB ordering, pending-color semantics and simultaneous USART traffic. Confirm the foreground only selects/services color, no per-bit or per-edge interrupt exists, and the minimal completion path cannot feed the watchdog.
4. Verify that only the lowest-priority foreground path feeds the watchdog. Deliberately stall the foreground loop and create sustained interrupt load; in both cases the watchdog must expire rather than being kept alive by an interrupt or callback.
5. Test power-up, brownout, watchdog reset, EEPROM bus faults and output fail-safe behavior.

### P4 — PCB and release gates

1. Freeze pin map, power supply, EEPROM footprint, pull-ups, RS-485 termination/biasing, programming header and safe output circuitry.
2. Verify the release ELF has no SRAM-allocated `.highcode` and no executable `.data` routine, and fits Flash and SRAM with measured worst-case stack margin and planned feature reserve.
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

Do not flash unvalidated EEPROM pin assignments or enable persistent writes until the exact part datasheet and hardware have been checked. Maintain a release checklist covering GPIO safety, absence of SRAM-resident executable code, RAM/stack margin, CRC rejection, EEPROM recovery, multidrop operation, timer/PWM/DMA WS2812 timing and resource ownership, foreground-only watchdog feeding and watchdog recovery.
