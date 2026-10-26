# Release checklist

- [x] Locked dependencies recorded and baseline `cargo firmware` succeeds.
- [x] Pure logic tests cover output mapping, Modbus CRC/parser, UID word order, and config corruption.
- [ ] Production schematic/pinout and output polarity approved.
- [ ] Interrupt-driven USART RX/TXE/TC and TIM2 bindings implemented and scope-tested.
- [ ] Reserved Flash page proven in linker map; erase/program/verify and power-loss tests passed.
- [ ] Isolated commissioning and duplicate-ID behavior bench-tested.
- [ ] IWDG timeout and reset-cause reporting fault-injected.
- [ ] WS2812 timing and serial-load coexistence scope-tested.
- [ ] RS485 soak/noise/power-cycle tests passed.
- [ ] Final ELF Flash/SRAM headroom recorded; `cargo flash` tested on the intended board.
