# Waveshare RP2040 Zero supervisor validation

This procedure records physical evidence for Stages 1 through 5 of
`docs/WAVESHARE_RP2040_ZERO_SUPERVISOR_PLAN.md`. A firmware build alone does
not pass any item below.

## Setup

- Record board revision, power source, GNU Arm toolchain version, Pico SDK
  version, host OS, native-USB port, USB-to-UART adapter, and test date.
- Connect adapter TX to GPIO1, RX to GPIO0, and ground to ground. Use 3.3 V
  signalling at 115200 8N1; do not connect the adapter's supply output.
- Connect a test LED with a suitable resistor and a logic analyser to each of
  GPIO2 through GPIO5 as required. Do not attach these loads during input tests.
- Build and install the UF2 with the documented board preset. Save the ELF,
  map, BIN, UF2, and complete build-size output with the results.

## Functional checks

1. Repeat 25 cold boots, 25 `reboot` commands, and 25 USB cable reconnects.
   Record enumeration failures and reset causes where observable.
2. Run `help`, `info`, `status`, `tasks`, and `queues` after each reset class.
   Confirm commands remain framed as `OK`/`ERR` and diagnostics as `LOG`.
3. For logical resources 0 through 3, verify input readings and output low/high
   levels. Confirm resource 4 and physical/reserved pin numbers are rejected.
4. Stop the flow and reset the board. Measure GPIO2 through GPIO5 as inputs
   without pulls and confirm their output latches do not produce a high pulse
   when subsequently configured as outputs.
5. Upload valid, corrupt, oversized, duplicate, out-of-order, and truncated
   images with `tools/microflow_uart_upload.py`. Confirm only a completely
   validated image replaces the active image.
6. Saturate UART receive and USB diagnostic output independently and together.
   Confirm drop/fault counters rise, commands recover, and the watchdog does
   not reset the board unexpectedly.

## Timing and soak record

- Capture interrupt-to-service latency, maximum service runtime, flow-cycle
  time, jitter, UART loss, USB throughput, stack high-water, and all queue
  high-water values. State instruments, sample counts, and worst-case traffic.
- Exercise the 32-bit microsecond timer wrap boundary or an equivalent injected
  boundary test, then run a minimum 24-hour combined UART/USB/GPIO soak.
- During the soak, periodically save `status`, `tasks`, and `queues`. Record any
  memory growth, queue corruption, watchdog reset, unintended activation, or
  WS2812 corruption. A failure leaves the relevant stage exit criterion open.
