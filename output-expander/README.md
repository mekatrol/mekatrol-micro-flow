# Output Expander C firmware

This is the canonical CH32V003F4P6 C firmware. The preserved Rust reference
and recovery implementation is in `../output-expander-rust/`.

Because this product has one fixed MCU and board, production sources use a
flat `src/` directory. Application, peripheral, startup, linker, and portable
logic are kept together without generic SoC/board/core abstraction layers.

The current firmware provides eight active-low outputs, RS-485 protocol v1
framing, interrupt-driven reception, a WS2812 status LED, and watchdog reset.
It implements `PING`, `GET_OUTPUTS`, `SET_OUTPUTS`, `RESET`, and UID-targeted
`ASSIGN_UID`. Address assignment is currently RAM-only: reset or power loss
restores the configured boot address (`0xFF` by default). `GET_STATUS`,
`GET_CONFIG`, and `SET_CONFIG` return `UNSUPPORTED` until status and EEPROM
integration land.

```sh
tools/clean.sh
tools/test.sh host-debug
tools/build.sh ch32v003-release
tools/flash.sh ch32v003-release
```

Factory-fresh devices use `0xFF` internally as the uncommissioned sentinel and
accept only `PING` at that address. Connect one uncommissioned device, read its
UID, then assign a temporary address:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0xFF
python3 tools/device_tool.py --port /dev/ttyUSB0 set-address \
  --uid 00112233445566778899aabb --new-address 0x18
```

Then use that commissioned address for normal commands:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0x18
python3 tools/device_tool.py --port /dev/ttyUSB0 outputs --address 0x18 --set 0x05
python3 tools/device_tool.py --port /dev/ttyUSB0 output --address 0x18 2 on
python3 tools/device_tool.py --port /dev/ttyUSB0 reset --address 0x18
```

The assigned address is verified with `PING`, but is not persistent.

The status LED pulses green while idle, shows blue for 300 ms after a valid
command, and flashes red after a malformed frame, UART error, or receive
overflow. A subsequent valid message clears the receive fault and shows the
blue command indication, matching the Rust reference behavior. The
`simulate-error` command sends an invalid-CRC frame to exercise the red fault
indication without eliciting a protocol response.

The WS2812 backend uses TIM1 and DMA so UART interrupts remain available while
the 24-bit waveform is transmitted.

See [docs/BUILD.md](docs/BUILD.md) for prerequisites, wiring, artifacts, and
recovery instructions. See [docs/protocol.md](docs/protocol.md) for the full
protocol contract and its current implementation status.
