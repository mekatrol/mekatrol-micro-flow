# Release checklist

- [x] CMake presets and pinned C toolchain configuration recorded.
- [x] Host tests cover output mapping, COBS/CCITT framing, command dispatch, commissioning, reset, and configuration field serialization.
- [x] Eight-channel GPIO driver implemented with safe initialization and coherent bitmask updates.
- [x] Host CLI matches the currently implemented command set.
- [x] `PING`, `GET_OUTPUTS`, `SET_OUTPUTS`, RAM-only `ASSIGN_ADDRESS_BY_UID`, and acknowledged `RESET` implemented.
- [ ] `GET_STATUS`, `GET_CONFIG`, and `SET_CONFIG` integrated with firmware state and EEPROM.
- [ ] Production schematic/pinout and output polarity approved.
- [ ] Eight output channels independently verified on a logic analyzer with glitch-free startup.
- [ ] Interrupt-driven USART RX/TXE/TC and TIM2 bindings implemented and scope-tested.
- [ ] External EEPROM erase/program/verify and power-loss tests passed.
- [ ] Isolated commissioning and duplicate-ID behavior bench-tested.
- [ ] IWDG timeout and reset-cause reporting fault-injected.
- [ ] WS2812 timing and serial-load coexistence scope-tested.
- [ ] RS485 soak/noise/power-cycle tests passed.
- [ ] Final ELF Flash/SRAM headroom recorded; C firmware flashing tested on the intended board.
