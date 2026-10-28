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
restores the configured boot address (`0x17` by default). `GET_STATUS`,
`GET_CONFIG`, and `SET_CONFIG` return `UNSUPPORTED` until status and EEPROM
integration land.

```sh
tools/clean.sh
tools/test.sh host-debug
tools/build.sh ch32v003-release
tools/flash.sh ch32v003-release
```

Use the matching host CLI after installing `pyserial`:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0x17
python3 tools/device_tool.py --port /dev/ttyUSB0 outputs --address 0x17 --set 0x05
python3 tools/device_tool.py --port /dev/ttyUSB0 output --address 0x17 2 on
python3 tools/device_tool.py --port /dev/ttyUSB0 reset --address 0x17
```

Temporary UID-targeted address assignment is also available:

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 set-address \
  --uid 00112233445566778899aabb --new-address 0x18
```

The assigned address is verified with `PING`, but is not persistent.

See [docs/BUILD.md](docs/BUILD.md) for prerequisites, wiring, artifacts, and
recovery instructions. See [docs/protocol.md](docs/protocol.md) for the full
protocol contract and its current implementation status.
