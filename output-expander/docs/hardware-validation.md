# Hardware validation

Status: desktop/source validation updated 2026-10-29; production PCB and bench
validation remains incomplete.

## Confirmed design constraints

- CH32V003F4P6 provides 16 KiB program Flash, 2 KiB SRAM, and a 96-bit
  electronic-signature UID at `0x1FFFF7E8`.
- The C firmware builds for RV32EC with the ILP32E ABI using project-owned
  startup assembly and linker script.
- USART1 uses PD5/TX and PD6/RX; PD1 is SWIO and PD7 can be NRST.
- The supplied schematic uses PC6 for RS-485 DE and PC7 for `/RE`.
- Firmware output artifacts are checked against the 16 KiB Flash and 2 KiB
  SRAM limits after linking.

## Remaining hardware gates

- Confirm the production PCB revision, all eight output routes, output-stage
  type and polarity, RS-485 controls, and WS2812 data connection.
- Confirm whether the populated output stage is ULN2803A, triac, or FET.
- Bench-check 19,200 8N1 baud error, DE timing through transmission complete,
  COBS framing, WS2812 voltage/timing, watchdog tolerance, and reset causes.
- Validate persistent-storage erase granularity, endurance, and power-failure
  behavior before enabling configuration writes.

Until these gates close, outputs must remain OFF during startup and all pin or
peripheral assumptions must be checked against the actual board revision.
