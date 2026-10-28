# Output Expander C Project Setup

## Scope

This document defines the developer tools for the canonical C implementation
of the CH32V003 output expander. During the parallel migration, the legacy Rust
firmware remains in `output-expander-rust/` and retains its own Rust setup
instructions. C and Rust build artifacts and clean operations must remain
independent.

The C project uses a modern upstream GNU RISC-V compiler with project-owned
startup and interrupt assembly. It does not require WCH's old patched GCC or
the proprietary `WCH-Interrupt-fast` compiler attribute.

## Required tools

All C builds require:

- Git;
- CMake 3.25 or newer;
- Ninja;
- a host C11 compiler;
- Python 3; and
- clang-format 15 or newer.

CH32V003 firmware builds additionally require:

- ****xPack GNU RISC-V Embedded GCC 14.3.0-1****;
- the `riscv-none-elf-*` programs from that distribution; and
- a supported WCH-LinkE flashing backend selected and pinned by Phase 0 of the
  migration plan.

Do not substitute `riscv32-unknown-elf-gcc`, `riscv64-unknown-elf-gcc`, WCH's
patched GCC, or another xPack release without treating the change as a
toolchain update. A compiler that accepts the source can still change interrupt
ABI behavior, instruction selection, code size, or runtime-library contents.

## Install the common build tools

Use the operating system's normal package manager for Git, CMake, Ninja,
Python, a host compiler, and clang-format. The cross-compiler is installed
separately from its pinned portable archive.

On Debian, Ubuntu, or Linux Mint, install the common packages with:

```sh
sudo apt update
sudo apt install git cmake ninja-build build-essential python3 clang-format
```

On macOS, install the Xcode command-line tools and use Homebrew for the common
utilities:

```sh
xcode-select --install
brew install cmake ninja python clang-format
```

On Windows, install Git and Python, then install CMake, Ninja, and LLVM through
their official installers, Visual Studio Build Tools, `winget`, or another
managed package source. Ensure `cmake`, `ninja`, `python`, `clang-format`, and a
host C compiler are available in a new terminal. Do not rely on a Visual Studio
developer shell for the cross-compiler path; configure that path explicitly as
described below.

## Install the pinned RISC-V cross-compiler

The CH32V003 firmware requires **xPack GNU RISC-V Embedded GCC 14.3.0-1**. Do not substitute the ESP-IDF `riscv32-esp-elf-gcc`, Ubuntu's `riscv64-unknown-elf-gcc`, or an unpinned xPack release. Download the compiler separately from the common build tools.

Official references:

- https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/tag/v14.3.0-1
- https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/docs/install/

### Linux x86-64: download, verify, and extract

Run these commands in order. **Downloading the archive does not install the compiler; the `tar` extraction step is essential.** The resulting installation is kept in a versioned directory under `~/opt`, independent of other projects' tool environments.

```sh
mkdir -p "$HOME/opt" "$HOME/Downloads"
cd "$HOME/Downloads"
wget https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v14.3.0-1/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz
```

Before extracting, check the downloaded archive against the SHA-256 checksum published with the official release. Record the verified checksum in the project dependency inventory. For example, once you have copied the expected SHA-256 value from the release page:

```sh
sha256sum xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz
# Compare the printed hash with the official published SHA-256 value.
```

**Extract the archive** (this is a required step):

```sh
tar -xzf xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz -C "$HOME/opt"
```

Confirm the executable was extracted to the expected location:

```sh
ls -l "$HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1/bin/riscv-none-elf-gcc"
```

If it is not there, inspect the actual extracted directory structure before changing `PATH`:

```sh
find "$HOME/opt" -type f -name 'riscv-none-elf-gcc' -print
```

### Linux: configure the shell PATH

For the current terminal session:

```sh
export PATH="$HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1/bin:$PATH"
```

For a persistent Bash setup, add the entry **once** to `~/.bashrc` (do not append it repeatedly on every setup attempt):

```sh
cat >> "$HOME/.bashrc" <<'EOF'

# CH32V003 firmware — xPack GNU RISC-V GCC 14.3.0-1
case ":$PATH:" in
  *":$HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1/bin:"*) ;;
  *) export PATH="$HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1/bin:$PATH" ;;
esac
EOF
source "$HOME/.bashrc"
```

If an entry for this toolchain already exists in `~/.bashrc`, update it instead of adding another block. Do not commit a developer-specific absolute path to `CMakePresets.json`.

### Verify the cross-compiler and build tools

Open a new terminal and run:

```sh
command -v cmake
command -v ninja
command -v riscv-none-elf-gcc
riscv-none-elf-gcc --version
riscv-none-elf-objcopy --version
riscv-none-elf-objdump --version
riscv-none-elf-size --version
```

`command -v riscv-none-elf-gcc` should point into `~/opt/xpack-riscv-none-elf-gcc-14.3.0-1/bin`, and GCC must report version 14.3.0. A directory being present in `PATH` does **not** prove the binary exists there. If `command -v` returns nothing, first check that the archive was extracted.

Confirm the required architecture/ABI flags are accepted:

```sh
printf 'void probe(void) {}\n' | \
  riscv-none-elf-gcc -x c -c -o /tmp/ch32v003-toolchain-probe.o - \
    -march=rv32ec_zicsr -mabi=ilp32e -ffreestanding
riscv-none-elf-objdump -f /tmp/ch32v003-toolchain-probe.o
```

Then, from `output-expander/`, discard any failed CMake configuration and build:

```sh
./tools/build.sh ch32v003-debug --clean
```

If CMake reports a missing `CMAKE_C_COMPILER`, first check the executable and `PATH`. A missing `CMAKE_ASM_COMPILER` may be a consequence of the missing C compiler. If CMake also reports that Ninja is missing, check `command -v ninja` and `ninja --version` separately; don't assume Ninja is absent solely from that CMake message.

### Other platforms

Suggested installation locations:

```text
Linux:   $HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1
macOS:   $HOME/opt/xpack-riscv-none-elf-gcc-14.3.0-1
Windows: %LOCALAPPDATA%\xPacks\riscv-none-elf-gcc\14.3.0-1
```

On macOS, download and extract the archive for the appropriate host architecture and configure the corresponding `bin` directory in the shell profile. On Windows PowerShell, after installing the matching archive in the suggested directory, a temporary session can use:

```powershell
$toolchain = Join-Path $env:LOCALAPPDATA 'xPacks\riscv-none-elf-gcc\14.3.0-1\bin'
$env:Path = "$toolchain;$env:Path"
```

The CMake toolchain file must locate the exact executable family:

```text
riscv-none-elf-gcc
riscv-none-elf-objcopy
riscv-none-elf-objdump
riscv-none-elf-size
riscv-none-elf-nm
```

It must check the compiler version and fail clearly if the selected compiler is not the pinned 14.3.0-1 distribution. The check should permit platform-specific suffix text printed by xPack while verifying GCC 14.3 and the xPack package version through the project toolchain manifest.

## CH32V003 compiler target

The CH32V003F4P6 uses the RV32E embedded register profile and compressed
instructions. Firmware compilation starts with:

```text
-march=rv32ec_zicsr
-mabi=ilp32e
-msmall-data-limit=0
-ffreestanding
-fno-builtin
-ffunction-sections
-fdata-sections
```

Release links use project-owned startup and linker files plus dead-section
removal:

```text
-nostartfiles
-Wl,--gc-sections
```

These options are owned by the CMake cross-toolchain and target definitions,
not copied into individual source directories. The completed linker setup must
produce ELF, map, and raw BIN artifacts and report use against 16 KiB Flash and
2 KiB SRAM.

`-march=rv32ec_zicsr` and `-mabi=ilp32e` are necessary but not sufficient
validation. Phase 0 must inspect the ELF attributes and disassembly, confirm the
reset/vector and interrupt wrappers, reject unsupported instructions, and run
the smoke image on the physical CH32V003.

## Interrupt ABI policy

The standard compiler does not provide WCH's proprietary
`interrupt("WCH-Interrupt-fast")` function attribute. The initial firmware
therefore keeps the CH32 hardware prologue/epilogue feature disabled.

Project-owned assembly performs reset/vector entry and any interrupt register
save/restore sequence required by the verified QingKe V2A ABI. Assembly wrappers
may call bounded C handler bodies. Every wrapper must be reviewed against its
generated disassembly and tested on hardware before interrupts are considered
working.

Do not enable hardware prologue/epilogue by copying a WCH example attribute into
C. Enabling it later is an architectural and toolchain change requiring its own
plan, compiler decision, ABI audit, latency measurement, and regression tests.

## Flashing tools

The board is programmed through WCH-LinkE using PD1/SWIO and common ground. The
target board is powered separately; leave the programmer's 3.3 V output
disconnected unless a reviewed hardware procedure explicitly says otherwise.

The final C flashing backend is a Phase 0 dependency decision. It must be
version-pinned, licensed for the project, callable non-interactively from Linux
and Windows wrappers where supported, and able to flash the generated ELF or
BIN without rebuilding it. Until that decision is recorded, do not claim the C
`flash` wrapper is production-ready.

During migration, retain the already proven Rust `wlink` setup with the legacy
firmware so the known-good Rust image can be restored. The C project must not
silently download a programmer or invoke Cargo during an ordinary C build.

## CH32V003 hardware debugging

The canonical C firmware uses the existing WCH-LinkE probe for hardware debugging through the CH32V003F4P6 PD1/SWIO connection.

### Debugging tools

The Linux development environment uses:

- xPack RISC-V GDB 14.3.0-1, installed with the pinned cross-compiler.
- WCH-compatible OpenOCD as the GDB server.
- WCH-LinkE connected through PD1/SWIO and common ground.
- The existing `ch32v003-debug` CMake configuration.

The system OpenOCD executable `/usr/bin/openocd` must not be assumed compatible merely because it is installed. Verify that it supports the WCH-LinkE and CH32V003 single-wire debug interface.

Check the available tools:

```sh
openocd --version
openocd -c "adapter list" -c shutdown
wlink --help
riscv-none-elf-gdb --version
```

Use the existing `wlink` flashing setup until an alternative has been explicitly verified.

### Debug build

Build with:

```sh
./tools/build.sh ch32v003-debug
```

The debug configuration should retain the project's target architecture and ABI:

```text
-march=rv32ec_zicsr
-mabi=ilp32e
```

For source-level debugging, use `-Og -g3` without removing the existing freestanding compilation settings, assembly startup, interrupt wrappers, or linker script.

The debug ELF must contain symbol information so GDB can inspect source variables and functions.

### GDB connection

Once a compatible OpenOCD server has been verified and started, connect using:

```sh
riscv-none-elf-gdb build/ch32v003-debug/output-expander.elf
```

Typical GDB commands, after connecting to the server:

```gdb
target extended-remote localhost:3333
monitor reset halt
break frame_receiver_poll
continue
```

Port 3333 is the conventional OpenOCD GDB port; confirm it in the running server's output.

Do not assume the generic OpenOCD installation supports CH32V003 debugging.

### Safety and hardware behavior

- The target board remains separately powered.
- The programmer's 3.3 V power output remains disconnected.
- Keep the PD1/SWIO connection and common ground.
- Debug breakpoints halt foreground execution and may also stop interrupt servicing.
- Halted firmware cannot reliably service incoming RS-485 traffic.
- Do not conduct receive-throughput or interrupt-latency measurements while halted.
- Preserve the verified 48 MHz clock initialization, including the explicit AHB prescaler configuration.
- Leave all eight active-low outputs OFF during initial debugging tests.

### Initial debugging acceptance tests

1. Connect OpenOCD to the WCH-LinkE and CH32V003.
2. Attach RISC-V GDB to the running server.
3. Halt and resume execution.
4. Inspect `debug_valid_packet_count`.
5. Send one valid packet over the USB-RS485 adapter.
6. Verify the packet counter increments.
7. Confirm corrupted packets do not increment the counter.
8. Verify all outputs remain OFF.

Hardware debugging is not considered configured until an actual GDB connection and target halt/resume have succeeded.

### Linux WCH-LinkE permissions

WCH-LinkE normally identifies as USB vendor `1a86`, product `8010`. Confirm the
actual attached device before installing a rule:

```sh
lsusb
```

If the confirmed device uses those identifiers, create a udev rule using the
distribution's normal administrator procedure, reload the rules, and reconnect
the programmer. Do not run build or flash tools with `sudo`; that risks
root-owned build artifacts and conceals incorrect device permissions.

## Hardware connection

For the current CH32V003F4P6 board, verify the schematic and board revision
before connecting:

\| WCH-LinkE | Target |
\|---|---|
\| SWDIO/SWIO | PD1/SWIO |
\| GND | GND |
\| 3V3 | Leave disconnected when the board is separately powered |

Power the board through its intended supply and keep NRST/PD7 accessible for
recovery. Phase 0 must document any board-revision differences rather than
encoding them as undocumented wiring assumptions.

## Verify the installation

Open a new terminal and run:

```sh
git --version
cmake --version
ninja --version
python3 --version
clang-format --version
riscv-none-elf-gcc --version
riscv-none-elf-objcopy --version
riscv-none-elf-objdump --version
riscv-none-elf-size --version
```

`riscv-none-elf-gcc --version` must report GCC 14.3.0 from the pinned xPack
installation. Also confirm the compiler accepts the exact architecture and ABI
without linking:

```sh
printf 'void probe(void) {}\n' |
  riscv-none-elf-gcc -x c -c -o /tmp/ch32v003-toolchain-probe.o - \\
    -march=rv32ec_zicsr -mabi=ilp32e -ffreestanding
riscv-none-elf-objdump -f /tmp/ch32v003-toolchain-probe.o
```

On Windows, the Phase 0 CMake compiler check provides the equivalent probe
without relying on `/tmp` or POSIX shell syntax.

## First build

After Phase 0 creates the canonical C project, run from `output-expander/`:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset ch32v003-debug
cmake --build --preset ch32v003-debug

cmake --preset ch32v003-release
cmake --build --preset ch32v003-release
```

The wrapper equivalents will be:

```sh
./tools/test.sh host-debug
./tools/build.sh ch32v003-debug
./tools/build.sh ch32v003-release --clean
```

On Windows:

```powershell
.\tools\test.ps1 host-debug
.\tools\build.ps1 ch32v003-debug
.\tools\build.ps1 ch32v003-release -Clean
```

These commands are the required interface the Phase 0 implementation must
create; they will not work until that build skeleton exists. See
`C_FIRMWARE_MIGRATION_PLAN.md` for the Phase 0 acceptance gate.

## Legacy Rust recovery setup during migration

Until Phase 0 moves the existing firmware to `output-expander-rust/`, its
checked-in `rust-toolchain.toml` installs the required nightly Rust components
when Cargo first runs. Install Rustup from <https://rustup.rs> and verify:

```sh
rustup --version
cargo --version
```

The currently proven recovery programmer is `wlink`. On Debian, Ubuntu, or
Linux Mint, install its native build prerequisites and then the tool:

```sh
sudo apt install libudev-dev libusb-1.0-0-dev
cargo install --git https://github.com/ch32-rs/wlink
wlink --version
```

From the legacy Rust project directory, verify the recovery build with:

```sh
cargo test --lib --target x86_64-unknown-linux-gnu
cargo firmware
```

Power the current board through USB-C with S2 on. Connect WCH-LinkE SWIO to
PD1 and connect ground; leave WCH-LinkE 3.3 V disconnected. The current Rust
flash command is:

```sh
cargo flash
```

Preserve these instructions with `output-expander-rust/` when the directory is
moved. They exist for A/B comparison and recovery and do not make Cargo or the
Rust compiler dependencies of the canonical C project.
