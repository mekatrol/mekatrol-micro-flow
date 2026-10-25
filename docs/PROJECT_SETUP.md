# Project Setup

## Required tools

All builds require:

- Git
- CMake 3.25 or newer
- Ninja
- a host C11 compiler
- Python 3
- clang-format 15 or newer for source formatting

Firmware builds additionally require:

- GNU Arm Embedded toolchain with `arm-none-eabi-gcc` for Pico and Pico 2 Arm
- a bare-metal `riscv32-unknown-elf-gcc` toolchain for Pico 2 RISC-V
- Raspberry Pi Pico SDK 2.3.1, including its TinyUSB submodule, for the Pico
  and Waveshare RP2040-Zero builds

Install the tools using the normal package manager or official toolchain
distribution for Windows, macOS, or Linux. Add the compiler executable
directories to `PATH`.

On Debian, Ubuntu, and Linux Mint, install clang-format with:

```sh
sudo apt install clang-format
```

See [FORMAT.md](FORMAT.md) for macOS and Windows installation instructions and
for the commands that format the source tree.

Set `PICO_SDK_PATH` to the Pico SDK 2.3.1 directory. The clean-build wrappers
also discover it in `~/.pico-sdk/sdk/2.3.1` and `~/sdk/pico-sdk-2.3.1`.
Pico 2 architecture smoke builds use source-controlled startup and linker
support and do not currently link the SDK.

## Verify installation

```sh
cmake --version
ninja --version
arm-none-eabi-gcc --version
riscv32-unknown-elf-gcc --version
python3 --version
clang-format --version
```

Only the compiler for the selected target is required. Host builds need neither
cross-compiler.

## First build

Run the host regression suite:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Build Pico firmware:

```sh
cmake --preset pico-debug
cmake --build --preset pico-debug
```

Build the Waveshare RP2040-Zero variant:

```sh
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

The two RP2040 presets compile the same firmware target with different status
LED mappings. The Pico uses a digital LED on GPIO25. The RP2040-Zero defines
`MICROFLOW_WAVESHARE_RP2040_ZERO` and drives its WS2812 RGB LED on GPIO16.

Build both Pico 2 architecture smoke targets:

```sh
cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug
cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

See [BUILD.md](BUILD.md) for artifacts and clean-build wrappers.
