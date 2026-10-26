# Hardware and HAL validation

Status: desktop/source validation completed 2026-10-26; production PCB and bench validation blocked.

## Verified from pinned sources

- `Cargo.lock` pins `ch32-hal` commit `bbcabaac96f482b1cfaea111d2fc938095b9a2fd` and `qingke-rt` 0.8.2.
- CH32V003F4P6 has 16 KiB program Flash, 2 KiB SRAM and a 96-bit electronic signature UID. The pinned HAL reads 12 bytes at `0x1FFFF7E8` through `signature::unique_id()`.
- Manufacturer TSSOP-20 signals include USART1 `PD5/UTX` and `PD6/URX`; PD1 is SWIO and PD7 can be NRST.
- The pinned HAL accepts blocking USART construction on RX PD6/TX PD5. Existing firmware builds with this mapping.
- HAL-generated `memory.x` defines 16 KiB at both the code alias and `0x0800_0000`; the final 64 bytes are **not reserved**. A local linker override has not yet been proven, so Flash writes remain disabled.
- The supplied schematic and current firmware use separate transceiver controls: PC6 is DE and PC7 is `/RE`. This conflicts with the draft plan's single-PC6 connection.
- `ch32-hal` provides critical-section integration and a Flash implementation, but its chip-specific erase/program behavior still needs bench validation before persistent writes are enabled.

## Unresolved hardware gates

- Confirm the production PCB revision and physical routing for all eight output channels, the output-stage type/polarity, RS485 DE and `/RE`, and WS2812 data.
- Confirm whether the board is ULN2803A, triac, or FET. The repository README says triac and FET variants; the plan assumes ULN2803A.
- Confirm Flash erase granularity/endurance from the applicable silicon revision, linker override strategy, and whether `wlink` mass-erases the reserved page.
- Bench-check 19,200 8E1 baud error, DE timing through TC, RTU gaps, WS2812 voltage/timing, LSI/IWDG tolerance, and reset-cause registers.

Until those gates close, GPIO names in `board.rs` are provisional metadata, outputs remain OFF at startup, and production Flash/IWDG/WS2812 bindings are intentionally not enabled.
