# Release checklist

- [x] CMake presets and pinned C toolchain configuration recorded.
- [x] Host tests cover output mapping, COBS/CCITT framing, command dispatch, commissioning, reset, and configuration field serialization.
- [x] Eight-channel GPIO driver implemented with safe initialization and coherent bitmask updates.
- [x] Host CLI matches the production command set.
- [x] `PING`, `GET_OUTPUTS`, `SET_OUTPUTS`, persistent `ASSIGN_ADDRESS_BY_UID`, and acknowledged `RESET` implemented.
- [x] `GET_STATUS`, `GET_CONFIG`, and `SET_CONFIG` integrated with firmware state and EEPROM.
- [ ] Production schematic/pinout and output polarity approved.
- [ ] Eight output channels independently verified on a logic analyzer with glitch-free startup.
- [ ] Interrupt-driven USART RX/TXE/TC and TIM2 bindings implemented and scope-tested.
- [x] External EEPROM page-write, ACK-poll, readback, dual-slot recovery, and power-loss behavior covered by firmware tests.
- [ ] Isolated commissioning and duplicate-ID behavior bench-tested.
- [ ] IWDG timeout and reset-cause reporting fault-injected.
- [ ] WS2812 timing and serial-load coexistence scope-tested.
- [ ] RS485 soak/noise/power-cycle tests passed.
- [ ] Final ELF Flash/SRAM headroom recorded; C firmware flashing tested on the intended board.
