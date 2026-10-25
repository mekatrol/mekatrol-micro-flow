# Testing Mekatrol Micro Flow

Host tests are the primary regression suite for portable code. A successful
firmware build is a compile/link check, not proof of hardware behavior.

## Host tests

The registered cases are:

| CTest name | Purpose |
|---|---|
| `microflow.version` | Public version API |
| `microflow.flow` | Portable flow-image format and validation |
| `microflow.upload-transport` | Framing, upload ordering, CRC, and activation |
| `microflow.console` | USB console command parsing and bounds |
| `microflow.board-gpio` | Pico logical GPIO mapping and safe states |

Run debug and release configurations:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

Run one case with:

```sh
ctest --preset host-debug -R '^microflow\.console$'
```

## Firmware compile checks

Changes affecting firmware should build all applicable targets:

```sh
cmake --preset pico-debug
cmake --build --preset pico-debug
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug
cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

Physical hardware evidence must record board revision, firmware commit,
toolchain version, procedure, duration, measurements, and result. Required
coverage for the current RP2040 firmware includes boot, USB enumeration,
console commands, task scheduling, stack high-water reporting, status output,
reboot, and sustained operation. Future capabilities require their own hardware
coverage when they are enabled.

## Adding tests

Place deterministic host tests under `tests/unit/`, register them in
`tests/unit/CMakeLists.txt`, enable the project warning and sanitizer helpers,
and add portable regressions for every portable bug fix.
