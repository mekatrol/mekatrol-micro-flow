# Waveshare RP2040 Zero

This partial target implements the Stage 1 supervisor described in
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
exchange is protected by a short interrupt mask. ISRs only acknowledge the
device and signal board hooks; UART and USB byte handling arrives in later
stages.

Build with:

```text
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

The build emits ELF, map, BIN, and UF2 files. Stage 1 host regressions cover
queue exhaustion, service ordering and timing, timer wraparound, idle waits,
watchdog points, fault delivery, and supervisor reinitialization as the warm
reset model. On-device extended runtime and repeated-reset evidence is still
required before promoting this partial board target to supported status.
