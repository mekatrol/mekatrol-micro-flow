# Building Mekatrol Micro Flow

## Presets

The repository has five public presets:

| Preset | Result |
|---|---|
| `host-debug` | Host tests with supported sanitizers |
| `host-release` | Optimized host tests |
| `pico-debug` | Pico RP2040 supervisor with GPIO25 status LED and USB CDC |
| `pico2-arm-debug` | Pico 2 Cortex-M33 compile/link smoke build |
| `pico2-riscv-debug` | Pico 2 Hazard3 RISC-V compile/link smoke build |

List them with:

```sh
cmake --list-presets=all
```

## Host build and tests

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

## Firmware builds

```sh
cmake --preset pico-debug
cmake --build --preset pico-debug

cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug

cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

The Pico build produces ELF, map, BIN, and UF2 files. It includes the bounded
supervisor, UART upload transport, native TinyUSB CDC console, logical GPIO,
watchdog, and GPIO25 status LED. Hold BOOTSEL while connecting the board and
copy the generated UF2 from `build/pico-debug/` to the mass-storage volume. A
normal build never flashes attached hardware.

The Pico 2 builds currently produce ELF, map, and BIN architecture smoke-test
artifacts. They are not yet flashable because RP2350 boot metadata and image
packaging are incomplete.

## Clean build wrappers

The wrappers delete the complete `build/` directory before configuring and
building one preset:

```sh
./build.sh pico-debug
./build.sh pico2-arm-debug
./build.sh pico2-riscv-debug
```

On Windows:

```powershell
.\build.ps1 pico-debug
```

Use direct preset commands when retaining multiple build directories matters.

## Prerequisites

See [PROJECT_SETUP.md](PROJECT_SETUP.md). The Pico build also requires Pico SDK
2.3.1 through `PICO_SDK_PATH`. Firmware builds require
`arm-none-eabi-gcc` for Pico and Pico 2 Arm, or
`riscv32-unknown-elf-gcc` for Pico 2 RISC-V. All builds use CMake 3.25 or
newer and Ninja.
