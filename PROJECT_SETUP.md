# Project Setup

## Required tools

All builds require:

- Git
- CMake 3.25 or newer
- Ninja
- a host C11 compiler

Firmware builds additionally require:

- GNU Arm Embedded toolchain with `arm-none-eabi-gcc` for Pico and Pico 2 Arm
- a bare-metal `riscv32-unknown-elf-gcc` toolchain for Pico 2 RISC-V
- Python 3 for the Pico image utilities
- Raspberry Pi Pico SDK 2.3.1, including its TinyUSB submodule, for Pico

Install the tools using the normal package manager or official toolchain
distribution for Windows, macOS, or Linux. Add the compiler executable
directories to `PATH`.

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

Build both Pico 2 architecture smoke targets:

```sh
cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug
cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

See [BUILD.md](BUILD.md) for artifacts and clean-build wrappers.
