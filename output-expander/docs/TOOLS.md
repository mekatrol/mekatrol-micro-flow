# Output Expander tool reference

Run these utilities from `output-expander/`. Shell scripts support Linux-style
environments; matching PowerShell scripts support Windows.

## `build.sh` and `build.ps1`

Configure and build `host-debug`, `host-release`, `ch32v003-debug`, or
`ch32v003-release` (the default). `--clean` or `-Clean` removes only that
preset before building.

```sh
tools/build.sh ch32v003-release --clean
```

Requires CMake and Ninja; target builds also need the pinned RISC-V toolchain.
See [BUILD.md](BUILD.md).

## `test.sh` and `test.ps1`

Build a host preset and run CTest with failure output. An optional test regular
expression limits the run.

```sh
tools/test.sh host-debug
tools/test.sh host-debug test_protocol
```

PowerShell uses `tools/test.ps1 -Preset host-debug -Test test_protocol`.

## `clean.sh` and `clean.ps1`

Remove the complete generated `build/` directory without touching sources.

```sh
tools/clean.sh
```

## `flash.sh` and `flash.ps1`

Build and flash `ch32v003-debug` or `ch32v003-release` (the default). The tool
checks the ELF then invokes `wlink` for CH32V003 at low SWIO speed. It requires
a WCH-LinkE and never runs during a normal build.

```sh
tools/flash.sh ch32v003-release
```

Wiring and recovery are in [BUILD.md#flash](BUILD.md#flash).

## `start_debug.sh`

Start WCH OpenOCD, wait for GDB port 3333, and open
`riscv-none-elf-gdb` against the existing debug ELF. It does not build or
flash. It rejects an occupied GDB port and stops OpenOCD when GDB exits.

```sh
tools/flash.sh ch32v003-debug
tools/start_debug.sh
```

`OPENOCD_BIN` and `GDB_BIN` override executable paths. See
[DEBUG.md](DEBUG.md).

## `device_tool.py`

Open a USB RS-485 adapter at 19,200 8N1 and exchange validated protocol-v1
frames. It requires `pyserial`. Global options are required `--port` and
optional response `--timeout`.

- `info --address ADDRESS`: read UID/version and, when commissioned, outputs.
- `outputs --address ADDRESS [--set MASK]`: read or replace all outputs.
- `output-delay --address ADDRESS [--set MILLISECONDS]`: read or set the
  staggered activation interval. Set values must be 500–10000 ms in 100 ms
  increments.
- `output --address ADDRESS CHANNEL on|off`: change one channel.
- `set-address --uid UID --new-address ADDRESS`: commission and verify an
  EEPROM-backed address.
- `reset --address ADDRESS`: request an acknowledged watchdog reset.
- `simulate-error --address ADDRESS`: send a bad-CRC frame to test fault
  indication; no response is expected.

```sh
python3 tools/device_tool.py --port /dev/ttyUSB0 info --address 0x23
python3 tools/device_tool.py --port /dev/ttyUSB0 output-delay --address 0x23 --set 1000
```

See [COMMISSIONING.md](COMMISSIONING.md) for the installation workflow.

## `test_rx_hardware.py`

Send a known serial test sequence through an adapter to exercise the physical
USART receive and error path. It is a board bring-up diagnostic, not a normal
commissioning step.

```sh
python3 tools/test_rx_hardware.py --port /dev/ttyUSB0
```

Use it with the checks in [DEBUG.md](DEBUG.md).

## `test_watchdog_reset.py`

Ping the device, send RESET, wait beyond the watchdog interval, and confirm the
same UID responds after reboot. Optional `--timeout` and `--reset-wait` values
must be positive. Run it only when restarting connected loads is safe.

```sh
python3 tools/test_watchdog_reset.py --port /dev/ttyUSB0 --address 0x23
```
