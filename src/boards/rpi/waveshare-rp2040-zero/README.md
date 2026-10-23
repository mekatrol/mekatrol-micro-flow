# Waveshare RP2040 Zero

This partial target implements the Stage 1 supervisor and Stage 2 UART upload
software described in
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
and `tools/microflow_uart_upload.py` for the host uploader. Native USB remains
a later stage.

Build with:

```text
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

The build emits ELF, map, BIN, and UF2 files. Stage 1 host regressions cover
queue exhaustion, service ordering and timing, timer wraparound, idle waits,
watchdog points, fault delivery, and supervisor reinitialization as the warm
reset model. On-device UART upload, overflow, disconnect, extended-runtime,
and repeated-reset evidence is still required before promoting this partial
board target or marking Stage 2 complete.
