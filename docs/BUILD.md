# Building Mekatrol Micro Flow

## Presets

The repository has six public presets:

| Preset | Result |
|---|---|
| `host-debug` | Host tests with supported sanitizers |
| `host-release` | Optimized host tests |
| `pico-debug` | Pico RP2040 supervisor with GPIO25 status LED and USB CDC |
| `waveshare-rp2040-zero-debug` | Waveshare RP2040-Zero supervisor with GPIO16 WS2812 status LED and USB CDC |
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

cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug

cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug

cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

The Pico builds produce ELF, map, BIN, and UF2 files. They include the bounded
supervisor, UART upload transport, native TinyUSB CDC console, logical GPIO,
and watchdog. The `pico-debug` preset drives the Pico's GPIO25 status LED; the
`waveshare-rp2040-zero-debug` preset drives the RP2040-Zero's WS2812 status LED
on GPIO16. Hold BOOTSEL while connecting the board and copy the generated UF2
from the selected preset's build directory to the mass-storage volume. A normal
build never flashes attached hardware.

The Waveshare UF2 is:

```text
build/waveshare-rp2040-zero-debug/microflow-raspberry-pi-pico.uf2
```

The executable name remains shared because this is a compile-time board
variant. Do not flash the `pico-debug` UF2 to the RP2040-Zero when status-LED
operation is required.

The Pico 2 builds currently produce ELF, map, and BIN architecture smoke-test
artifacts. They are not yet flashable because RP2350 boot metadata and image
packaging are incomplete.

## Clean build wrappers

The wrappers delete the complete `build/` directory before configuring and
building one preset:

```sh
./tools/build.sh pico-debug
./tools/build.sh waveshare-rp2040-zero-debug
./tools/build.sh pico2-arm-debug
./tools/build.sh pico2-riscv-debug
```

On Windows:

```powershell
.\tools\build.ps1 pico-debug
.\tools\build.ps1 waveshare-rp2040-zero-debug
```

Use direct preset commands when retaining multiple build directories matters.

To remove the complete `build/` directory without starting another build, run:

```sh
./tools/clean.sh
```

or on Windows:

```powershell
.\tools\clean.ps1
```

## Prerequisites

See [PROJECT_SETUP.md](PROJECT_SETUP.md). The Pico build also requires Pico SDK
2.3.1 through `PICO_SDK_PATH`. Firmware builds require
`arm-none-eabi-gcc` for Pico and Pico 2 Arm, or
`riscv32-unknown-elf-gcc` for Pico 2 RISC-V. All builds use CMake 3.25 or
newer and Ninja.
