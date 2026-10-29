# Output Expander firmware architecture

## Product overview

The Output Expander is an eight-channel, active-low output module for a
half-duplex RS-485 automation network. A CH32V003F4P6 starts every load safely,
restores configuration from EEPROM, processes commands, controls the outputs,
drives a WS2812 status LED, enforces a communications fail-safe, and uses an
independent watchdog to recover from stalled software.

The firmware is freestanding C11: it has no operating system, heap, vendor
framework, or C library dependency. Project-owned startup assembly and linker
script place code in 16 KiB Flash and data in 2 KiB SRAM. All buffers have
fixed capacity. This guide describes the completed product; wire details are
specified in [protocol.md](protocol.md).

## Hardware and pin allocation

The internal 24 MHz HSI and PLL provide a 48 MHz system clock. PA1/XI and
PA2/XO remain reserved for an optional crystal. PD1/SWIO and PD7/NRST remain
dedicated to programming and reset.

| Function | Pin | Peripheral/mode | Behavior |
|---|---|---|---|
| Outputs 1–4 | PC0, PC3, PC4, PC5 | GPIO output | Active LOW; HIGH is OFF |
| Outputs 5–8 | PD0, PD2, PD3, PD4 | GPIO output | Active LOW; HIGH is OFF |
| EEPROM clock/data | PC2/PC1 | I2C1 SCL/SDA | Open-drain, 3.3 V pull-ups |
| Status LED | PC7 | TIM1_CH2, remap group 1 | WS2812 data, idle LOW |
| RS-485 TX/RX | PD5/PD6 | USART1 | 19,200 bit/s, 8N1 |
| RS-485 direction | PC6 | GPIO output | LOW receive, HIGH transmit |
| Programming/reset | PD1/PD7 | SWIO/NRST | Debug and external reset |

The assignments keep storage on I2C1 and timing-sensitive LED generation on a
timer/DMA path. USART reception uses interrupts, leaving DMA1 channel 3
dedicated to the LED.

## Boot and execution model

`startup.S` establishes the stack, copies `.data`, clears `.bss`, installs
interrupt entries, and calls `main`. Initialization is deliberately ordered:

1. Configure the 48 MHz clock and free-running SysTick.
2. Latch all output pins HIGH before changing them to output mode.
3. hold RS-485 DE and `/RE` LOW so the transceiver cannot drive the bus.
4. Initialize output, LED, receive queue, frame decoder, I2C, and USART state.
5. Read the MCU's immutable 96-bit UID.
6. Validate both EEPROM slots and select the newest valid configuration, or
   safe factory defaults when storage is blank.
7. Apply the stored startup mask only after GPIO and configuration are valid.
8. Configure the programmable voltage detector (PVD) and its dedicated
   interrupt before enabling global interrupt delivery.
9. Enable USART receive/error interrupts and start the watchdog.

The USART ISR only captures a byte or error marker in a bounded ring buffer.
The foreground owns framing, commands, EEPROM transactions, transmission,
timeouts, LED policy, and watchdog feeding. This keeps interrupt latency short
and prevents an ISR from concealing a stalled application.

## Clocks, timers, and DMA

### SysTick

SysTick is a free-running 32-bit monotonic counter at 48 MHz. Unsigned elapsed
comparisons tolerate wraparound; 48,000 ticks equal one millisecond. The same
timebase supports heartbeat, command indication, communications timeout, and
WS2812 reset-low deadlines. Drivers never reset or temporarily repurpose it.

### TIM1 and DMA1 channel 3

TIM1 channel 2 generates the 800 kHz WS2812 waveform on PC7. With prescaler
zero and ARR 59, each period is 60 clocks (1.25 microseconds). CCR2 values of
18 and 36 produce the zero and one high times. Compare preload changes duties
only on a defined PWM boundary.

DMA1 channel 3 writes 16-bit duties to TIM1_CCR2. A static buffer holds 24 GRB
data duties plus LOW tail entries. The CPU completes it before transfer and
never modifies it while DMA owns it. Foreground completion service disables
DMA and PWM, forces PC7 LOW, and maintains a 300 microsecond latch interval.
There is no per-bit interrupt, so USART reception remains responsive.

TIM2 is unallocated. USART1 RX is interrupt-driven and I2C1 uses bounded
foreground transactions; neither consumes a DMA channel.

## Output subsystem

The application uses an eight-bit logical mask: bit 0 is channel 1, bit 7 is
channel 8, one is ON, and zero is OFF. `output_driver.c` owns separate requested
and applied masks and converts applied changes to active-low levels across
GPIOC/GPIOD. All pins reach OFF before any startup mask is requested.

### Output activation sequencing

The output driver enforces the solenoid-transformer inrush-current constraint;
it does not rely on controller message cadence. `SET_OUTPUTS` replaces the
complete requested mask. Channels removed by the new mask are switched OFF
immediately, while newly requested channels remain pending. A non-blocking
foreground service compares the two masks and energizes at most one pending
channel after the configured interval has elapsed since the preceding
OFF-to-ON transition.

The first channel after boot may energize immediately. Further channels are
selected in round-robin order and are separated by the configured 500–10000 ms
interval. The factory default is 500 ms. A delayed foreground iteration never
"catches up" by enabling several overdue channels: every service call can issue
at most one single-bit `hal_output_on()` operation and restarts the interval.
This makes one command requesting eight channels, eight separate commands, and
any mixture of partial-mask commands electrically equivalent.

A newer requested mask cancels pending channels from older requests rather
than queueing stale commands. `SET_OUTPUTS` acknowledges the accepted target
mask promptly, while `GET_OUTPUTS` reports the mask actually applied to GPIO.
This avoids holding an RS-485 transaction open for a ramp that may require
several seconds. `GET_OUTPUT_DELAY` and `SET_OUTPUT_DELAY` read and update the
persist the interval as one byte through the transactional EEPROM configuration
mechanism. That byte counts 100 ms units, with valid values from 5 through 100;
unsafe values below 5 are rejected before storage or activation. The driver
expands the configured unit count into SysTick ticks only for runtime timing.

Brownout and reset paths de-energize outputs without delay and suppress the
sequencer while recovery is pending. OFF operations are never rate-limited;
only inrush-producing OFF-to-ON transitions are spaced.

`SET_OUTPUTS` changes live state only and never writes EEPROM. If qualifying
traffic stops for the configured interval, the persistent timeout mask is
applied and timeout status is raised. Timeout zero disables monitoring. A
valid addressed `PING` or `SET_OUTPUTS` clears the condition and restarts the
deadline. The first deadline starts after boot configuration is loaded.

## EEPROM and persistent configuration

The local SOT-23-5 24C02-class EEPROM is a 2 Kbit (256-byte) I2C device on
PC1/PC2. It stores configuration separately from program Flash. Persistent
fields are the commissioned address, startup mask, timeout in milliseconds,
timeout mask, LED brightness, one-byte output activation delay in 100 ms units,
schema version, sequence number, and CRC-32.
Serialization is explicit; native C structure layout is never stored.

| Range | Allocation |
|---|---|
| `0x00..0x3F` | Configuration slot A (64 bytes) |
| `0x40..0x7F` | Configuration slot B (64 bytes) |
| `0x80..0xFF` | Reserved for compatible expansion |

At boot, both slots are checked for magic, version, field ranges, and CRC.
Wrap-aware sequence comparison chooses the newer valid record. If only one is
valid it is used. If neither is valid, the device uses address `0xFF`, all
outputs OFF, timeout disabled, and an uncommissioned/storage indication.

Persistent updates are power-fail-safe transactions:

1. Validate all requested fields in RAM.
2. Build a record with the next sequence number.
3. Write the inactive slot without crossing EEPROM page boundaries.
4. ACK-poll each page with a bounded deadline.
5. Read back and validate the complete record and CRC.
6. Activate the new values in RAM and report success.

The previous valid slot is never overwritten first. A write timeout or failed
readback keeps the old configuration active, raises storage-fault status, and
returns `STORAGE_ERROR`. An unchanged request performs no write. UID-directed
address assignment uses this same commit-before-activate path.

## RS-485 communications

USART1 operates at 19,200 bit/s, 8N1. PC6 controls tied DE and `/RE`: it stays
LOW while receiving, goes HIGH for a response, and returns LOW only after the
USART transmission-complete flag confirms the final stop bit has left. The
master initiates every transaction and devices never answer broadcasts.

The ISR places bytes/errors into a fixed ring. Foreground code collects through
the `0x00` delimiter, COBS-decodes, then validates version, lengths, type, and
CRC-16/CCITT-FALSE. Bad COBS/CRC/version, oversize frames, wrong destinations,
and non-request packets are silently discarded. The maximum decoded frame is
32 bytes with a 24-byte payload. CRC uses polynomial `0x1021`, init `0xFFFF`,
no reflection, and xorout zero (`123456789` gives `0x29B1`).

| Command | Purpose |
|---|---|
| `PING` | Return UID/version and refresh heartbeat |
| `GET_STATUS` | Return output/input snapshots and health flags |
| `SET_OUTPUTS` / `GET_OUTPUTS` | Change or read live outputs |
| `GET_CONFIG` / `SET_CONFIG` | Read or transactionally commit settings |
| `ASSIGN_ADDRESS_BY_UID` | Persist an address on the matching device |
| `RESET` | Acknowledge, release the bus, then permit watchdog reset |

Payload layouts, error codes, and routing rules are in
[protocol.md](protocol.md).

## Identity and commissioning

Every MCU has a factory 96-bit UID. Address `0xFF` denotes an uncommissioned
device, `0x00` is broadcast, and `0x01..0xFE` are normal addresses. A host
reads an isolated new unit's UID with `PING` to `0xFF`, then broadcasts the UID
and selected address. Only the complete UID match commits it, and broadcasts
remain silent. The host verifies by pinging the new address. Only one new unit
is connected during discovery because multiple `0xFF` replies would collide.

## Status LED

| Indication | Meaning |
|---|---|
| Unconfigured pattern | No valid commissioned configuration |
| Gentle green pulse | Healthy operation |
| Blue for 300 ms | A valid command was accepted |
| Amber pattern | Communications timeout and fail-safe outputs active |
| Red pattern | Receive/protocol or EEPROM storage fault |

Brightness comes from EEPROM. `status_led.c` chooses policy,
`ws2812.c` implements a non-blocking latest-color-wins state machine, and
`ws2812_hw.c` owns TIM1/DMA. Repeated requests for an existing color do no work.

## Watchdog and fault containment

The CH32V003 programmable voltage detector monitors the MCU's regulated 3.3 V
VCC rail (the board's 6219P33M regulator output), not the upstream 5 V input.
Firmware selects PLS `001`, which asserts on a falling supply at approximately
2.9 V and deasserts on recovery at approximately 3.05 V. This retains margin
above the MCU's 2.7 V minimum operating voltage. The detector is not
active after hardware reset; `pvd_init()` explicitly enables the PWR peripheral
clock, PVD, EXTI line 8 rising-edge trigger, and dedicated PFIC interrupt 17.
Initialization also reads the live PVD status so a rail that is already low
cannot evade the edge-triggered interrupt.

The PVD ISR performs only bounded fail-safe work: it latches the brownout state,
atomically drives all eight active-low outputs OFF, clears the EXTI flag, and
returns. The latch prevents foreground command processing from re-energizing an
output after the event. It clears only at reset; foreground watchdog feeding is
withheld after brownout so the independent watchdog restarts the controller.
If the rail remains unsafe, startup detection keeps the outputs OFF and the
device repeats this fail-safe recovery rather than resuming normal operation.

The independent watchdog uses the nominal 128 kHz LSI divided by 128 with
reload 4095, giving a nominal 4.096-second expiry independent of the main
clock. Only the final step of a completed foreground loop feeds it; no ISR or
DMA path can. A stalled application therefore resets instead of appearing
healthy. Expected peripheral waits are bounded so ordinary faults can return
to the loop and be reported.

`RESET` sends its success response, waits for USART TC, releases RS-485, then
permanently disables watchdog feeding. No queued command executes while reset
is pending. Receive overflow, UART/framing error, communications timeout, and
storage failure are surfaced through status/LED state and never cause partial
output or configuration changes.

## Module ownership

| Area | Modules | Responsibility |
|---|---|---|
| Runtime | `startup.S`, `ch32v003.ld`, `freestanding.c` | Reset and memory/runtime support |
| MCU | `registers.h`, `hal.c` | Registers, clocks, safe GPIO, SysTick |
| Outputs | `output_state.c`, `output_driver.c` | Logical mask and active-low GPIO |
| Serial | `usart.c`, `usart_irq.c`, `rs485.c`, `irq_rx.c` | USART, direction, bounded ISR queue |
| Protocol | `frame_receiver.c`, `cobs.c`, `crc16.c`, `protocol.c` | Framing and validation |
| Commands | `dispatcher.c`, `command_processor.c`, `comms_app.c` | Routing, execution, responses |
| Configuration | `config_fields.c`, EEPROM/store modules | Codec, I2C, dual-slot persistence |
| Identity | `device_uid.c`, `commissioning.c` | UID and targeted assignment |
| LED | `status_led.c`, `ws2812.c`, `ws2812_hw.c` | Policy and timer/DMA waveform |
| Recovery | `pvd.c`, `watchdog.c` | Brownout containment and independent watchdog reset |

Portable codecs and policy never access hardware; drivers do not interpret
commands; `main` composes modules and owns initialization and scheduling.

## Safety and resource invariants

- There is no dynamic allocation, recursion, or unbounded input buffer.
- All outputs are OFF before their GPIO pins become actively driven.
- Only validated unicast commands alter live outputs.
- Only the foreground output sequencer creates OFF-to-ON transitions; each HAL
  activation contains exactly one channel and consecutive activations respect
  the configured delay.
- Settings become active only after verified EEPROM commit.
- Broadcasts never receive replies; RS-485 is released only after USART TC.
- Timer/DMA generates every WS2812 pulse.
- Only healthy foreground progress feeds the watchdog.
- A detected brownout is latched until reset and immediately forces every
  output OFF.

Builds enforce the 16 KiB Flash and 2 KiB static SRAM limits. Host tests cover
portable framing, commands, configuration, commissioning, outputs, and LED
behavior. Physical validation is tracked in
[hardware-validation.md](hardware-validation.md).
