# Output Expander firmware

Production C firmware and support utilities for the CH32V003F4P6 eight-channel
RS-485 Output Expander. The device provides eight active-low outputs,
persistent EEPROM configuration, a COBS/CRC-protected protocol,
communications fail-safe behavior, a DMA-driven WS2812 status LED, and
independent-watchdog recovery.

Start with the [architecture guide](docs/ARCHITECTURE.md). To install a new
unit, follow the plain-language [commissioning guide](docs/COMMISSIONING.md).

## Common development commands

Run these from `output-expander/`:

```sh
tools/clean.sh
tools/test.sh host-debug
tools/build.sh ch32v003-release
tools/flash.sh ch32v003-release
```

PowerShell equivalents are provided for clean, test, build, and flash.
Generated files stay below `build/<preset>/`.

## Tools

| Tool | Purpose | Details |
|---|---|---|
| `build.sh`, `build.ps1` | Build a host or CH32V003 CMake preset, optionally from clean state | [Build guide](docs/BUILD.md), [tool reference](docs/TOOLS.md#buildsh-and-buildps1) |
| `test.sh`, `test.ps1` | Build a host preset and run all or selected unit tests | [Tool reference](docs/TOOLS.md#testsh-and-testps1) |
| `clean.sh`, `clean.ps1` | Remove the generated `build/` tree | [Tool reference](docs/TOOLS.md#cleansh-and-cleanps1) |
| `flash.sh`, `flash.ps1` | Build and program an ELF with WCH-LinkE and `wlink` | [Flash guide](docs/BUILD.md#flash), [tool reference](docs/TOOLS.md#flashsh-and-flashps1) |
| `start_debug.sh` | Start WCH OpenOCD and GDB for the debug ELF | [Debug guide](docs/DEBUG.md), [tool reference](docs/TOOLS.md#start_debugsh) |
| `device_tool.py` | Discover, commission, inspect, control, reset, and fault-test a device | [Commissioning](docs/COMMISSIONING.md), [tool reference](docs/TOOLS.md#device_toolpy) |
| `test_rx_hardware.py` | Exercise and diagnose the physical USART receive path | [Debug guide](docs/DEBUG.md), [tool reference](docs/TOOLS.md#test_rx_hardwarepy) |
| `test_watchdog_reset.py` | Verify acknowledged watchdog reset and recovery on hardware | [Tool reference](docs/TOOLS.md#test_watchdog_resetpy) |

## Documentation

| Document | Purpose |
|---|---|
| [Architecture](docs/ARCHITECTURE.md) | Pins, peripherals, timers, DMA, EEPROM, watchdog, runtime, and modules |
| [Commissioning](docs/COMMISSIONING.md) | Non-technical installation and output test procedure |
| [Protocol](docs/protocol.md) | Exact RS-485 frames, commands, status, and errors |
| [Tool reference](docs/TOOLS.md) | Every utility under `tools/`, with options and examples |
| [Build and flash](docs/BUILD.md) | Prerequisites, presets, artifacts, and programming |
| [Project setup](docs/PROJECT_SETUP.md) | Reproducible compiler and development environment |
| [Debugging](docs/DEBUG.md) | OpenOCD/GDB setup and low-level troubleshooting |
| [Dependencies](docs/DEPENDENCIES.md) | Pinned tools and vendor documents |
| [Hardware validation](docs/hardware-validation.md) | PCB and bench checks for hardware design |
| [Release checklist](docs/release-checklist.md) | Evidence required for release |

The firmware targets one MCU and board, so production sources remain in the
flat `src/` directory rather than generic board or SoC abstraction layers.
