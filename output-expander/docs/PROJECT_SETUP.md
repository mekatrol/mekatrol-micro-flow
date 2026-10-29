# Output Expander C project setup

## Required tools

Install Git, CMake 3.25 or newer, Ninja, Python 3, clang-format 15 or newer,
and a host C11 compiler. Firmware builds additionally require xPack GNU RISC-V
Embedded GCC 14.3.0-1 and its `riscv-none-elf-*` tools.

The cross-compiler version is pinned because ABI behavior, instruction
selection, runtime helpers, and firmware size can vary between releases. Add
its `bin` directory to `PATH`, then verify:

```sh
cmake --version
ninja --version
clang-format --version
riscv-none-elf-gcc --version
riscv-none-elf-objcopy --version
riscv-none-elf-objdump --version
riscv-none-elf-size --version
```

The compiler must accept the CH32V003 architecture and ABI:

```sh
printf 'void probe(void) {}\n' |
  riscv-none-elf-gcc -x c -c -o /tmp/ch32v003-toolchain-probe.o - \
    -march=rv32ec_zicsr -mabi=ilp32e -ffreestanding
riscv-none-elf-objdump -f /tmp/ch32v003-toolchain-probe.o
```

## Build and test

From `output-expander/`:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset ch32v003-debug
cmake --build --preset ch32v003-debug
cmake --preset ch32v003-release
cmake --build --preset ch32v003-release
```

The scripts in `tools/` provide the supported shortcuts for clean, test,
firmware build, flash, and debug operations. Generated files stay under
`build/<preset>/`.

## Compiler and interrupt policy

The CH32V003 uses `-march=rv32ec_zicsr` and `-mabi=ilp32e`. Project-owned
startup assembly, interrupt wrappers, and the linker script define the target
runtime. Release validation must inspect ELF attributes and disassembly and
must exercise reset and interrupt handling on physical hardware.

## Flash and debug connection

Connect WCH-LinkE SWIO to PD1/SWIO and GND to GND. Power the target through its
intended supply and leave the programmer's 3.3 V output disconnected when the
board is independently powered. Build and flash with:

```sh
tools/build.sh ch32v003-release
tools/flash.sh ch32v003-release
```

For source debugging, build `ch32v003-debug`, start the configured OpenOCD
server, and connect `riscv-none-elf-gdb` to the generated ELF. Confirm that the
OpenOCD installation supports WCH-LinkE and the CH32V003 single-wire interface;
a generic system OpenOCD package is not necessarily compatible.

Debug halts stop foreground work and may stop interrupt service. Do not measure
RS-485 throughput or interrupt latency while halted, and keep all active-low
outputs OFF during initial hardware tests.
