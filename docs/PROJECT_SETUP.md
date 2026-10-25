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

## FreeRTOS kernel submodule

FreeRTOS is a firmware-only dependency. Keep the portable code under `src/lib`
independent of FreeRTOS. The kernel is installed as a Git submodule at
`third_party/FreeRTOS-Kernel` and pinned to an exact release commit. FreeRTOS
release names such as `V11.3.0` are tags, not branches; do not pass a release
tag to `git submodule add -b`. The shared `pico-base` CMake preset sets
`FREERTOS_KERNEL_PATH` to `${sourceDir}/third_party/FreeRTOS-Kernel`, so no
shell environment variable or command-line cache override is required.

### Add the pinned kernel to this repository

Run these commands from the repository root. The `--force` option is required
because `/third_party/` is otherwise ignored for developer-local dependencies.

```sh
git submodule add --force \
  https://github.com/FreeRTOS/FreeRTOS-Kernel.git \
  third_party/FreeRTOS-Kernel
git -C third_party/FreeRTOS-Kernel fetch --tags origin
git -C third_party/FreeRTOS-Kernel switch --detach V11.3.0
git -C third_party/FreeRTOS-Kernel submodule update --init --recursive
git add .gitmodules third_party/FreeRTOS-Kernel
git diff --cached --submodule
git commit -m "Add FreeRTOS kernel V11.3.0"
```

Confirm that the submodule is at the intended release:

```sh
git -C third_party/FreeRTOS-Kernel describe --tags --exact-match
git submodule status --recursive
```

The first command must print `V11.3.0`. A leading `-` in `git submodule
status` means that a submodule has not been initialized; a leading `+` means
that its checkout does not match the commit recorded by this repository.

### Clone a repository that already contains the submodule

For a new clone, initialize all submodules in the clone operation:

```sh
git clone --recurse-submodules \
  https://github.com/mekatrol/mekatrol-micro-flow.git
cd mekatrol-micro-flow
git submodule status --recursive
```

For an existing clone, or after switching to a revision that changes the
pinned FreeRTOS commit, run:

```sh
git submodule sync --recursive
git submodule update --init --recursive
```

### Update FreeRTOS to a new release

First fetch the upstream tags and inspect the available releases:

```sh
git -C third_party/FreeRTOS-Kernel fetch --tags --prune origin
git -C third_party/FreeRTOS-Kernel tag --list 'V*' \
  --sort=-version:refname | head -n 20
```

After selecting and reviewing a release, replace `V11.3.1` below with its
actual tag, check out the tag in detached-HEAD state, and initialize any nested
dependencies added by that release:

```sh
git -C third_party/FreeRTOS-Kernel switch --detach V11.3.1
git -C third_party/FreeRTOS-Kernel submodule update --init --recursive
git -C third_party/FreeRTOS-Kernel describe --tags --exact-match
git add third_party/FreeRTOS-Kernel
git diff --cached --submodule
git commit -m "Update FreeRTOS kernel to V11.3.1"
```

Build and test every supported firmware target before pushing the update. To
abandon an uncommitted update and restore the version recorded by the parent
repository, run:

```sh
git submodule update --init --recursive \
  third_party/FreeRTOS-Kernel
```

Do not use `git submodule update --remote` for release updates. That follows a
moving branch instead of selecting and recording a reviewed release tag.

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
Both builds use the FreeRTOS RP2040 SMP port and statically allocated USB and
status tasks.

Build both Pico 2 architecture smoke targets:

```sh
cmake --preset pico2-arm-debug
cmake --build --preset pico2-arm-debug
cmake --preset pico2-riscv-debug
cmake --build --preset pico2-riscv-debug
```

See [BUILD.md](BUILD.md) for artifacts and clean-build wrappers.
