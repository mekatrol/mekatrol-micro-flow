# Waveshare RP2040 Zero

This partial target implements the Stage 1 supervisor, Stage 2 UART upload,
the board-owned portion of the Stage 3 USB CDC console, and the initial Stage 4
logical GPIO adapter described in
`docs/WAVESHARE_RP2040_ZERO_SUPERVISOR_PLAN.md`. It uses RP2040 Timer Alarm 0
to wake the main-context supervisor, services the watchdog between dispatch
passes, and sleeps with `WFI` while idle. GPIO16 remains the board-owned WS2812
status indicator.

The supervisor has no dynamic allocation. Its service table is ordered by
numeric priority (lower runs first), and each ready service runs to completion.
Runtime budgets are measured with the wrapping 32-bit microsecond timer.

The byte and compact-event rings are single-producer/single-consumer queues:
one peripheral ISR owns `write_index`, and one supervisor service owns
`read_index`. Full queues reject the newest item, retain unread content,
increment an overflow counter, update their high-water mark, and—when pushed
through the supervisor wrapper—raise the fault event. Main-context event flag
exchange is protected by a short interrupt mask.

The project-owned UART0 driver maps GPIO0/GPIO1 at 115200 8N1. Its interrupt
path moves bytes through board-owned SPSC rings; frame parsing, CRC checking,
staging, validation, and activation run in supervisor context. The active RAM
image is replaced only after complete validation. See
`docs/WAVESHARE_RP2040_ZERO_UART_PROTOCOL.md` for the wire and image formats
and `tools/microflow_uart_upload.py` for the host uploader.

The failed project-owned register-level USB driver has been replaced by pinned
Pico SDK 2.3.1 TinyUSB CDC device support behind the RP2040/board boundary.
Hardware enumeration and soak-test evidence is still required before Stage 3
is complete. Console lines remain bounded at 79 characters plus their
terminator; command responses use `OK`/`ERR` records and asynchronous
diagnostics use `LOG` records. Logical GPIO resources 0 through 3 map privately
to GPIO2 through GPIO5. Console and future flow operations cannot address
physical pins directly; UART GPIO0/GPIO1 and status-LED GPIO16 are reserved.
General-purpose resources start as unpulled inputs and return to that safe state
when a flow is stopped. The status WS2812 remains a separate logical indicator.

Build with:

```text
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

The build emits ELF, map, BIN, and UF2 files. Stage 1 host regressions cover
queue exhaustion, service ordering and timing, timer wraparound, idle waits,
watchdog points, fault delivery, and supervisor reinitialization as the warm
reset model. On-device UART upload, USB enumeration/reconnect across supported
host systems, overflow, concurrent-load, extended-runtime, and repeated-reset
evidence is still required before promoting this partial board target or
claiming all stage exit criteria complete.

The post-build size check reports flash and static RAM and fails above the
RP2040's configured 2 MiB flash or 264 KiB SRAM limits. The `tasks` console
command reports service dispatch/runtime maxima and watchdog servicing;
`queues` reports UART and USB high-water/drop counters.
