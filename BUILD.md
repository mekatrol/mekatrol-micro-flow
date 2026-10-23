# Building Mekatrol Micro Flow

This guide covers configuration, compilation, firmware artifacts, and cleaning.
For the complete test inventory and instructions for running individual tests,
see [TEST.md](TEST.md).

## Prerequisites

Start with [PROJECT_SETUP.md](PROJECT_SETUP.md). It is the canonical setup guide
for clean Windows, macOS, and Linux machines and covers host tools, pinned Arm
and Espressif cross-compilers, esptool, environment activation, USB access, and
installation verification.

The commands below assume those prerequisites are installed, the required
cross-compiler is on `PATH`, and the current directory is the repository root.
`ctest` is installed with CMake. Except where a shell is named explicitly, the
CMake and CTest commands work in PowerShell, Command Prompt, and POSIX shells.

## Clean build scripts

The repository provides scripts that completely remove `build/`, configure one
implemented target, and compile it. Accepted targets are the two host presets,
the two experimental Pico 2 architecture presets, and the three Waveshare
firmware presets documented in
[PROJECT_SETUP.md](PROJECT_SETUP.md).

On Linux or macOS, run:

```sh
./build.sh host-debug
./build.sh host-release
```

On Windows PowerShell, run:

```powershell
.\build.ps1 host-debug
.\build.ps1 host-release
```

The single argument is the target name. Each script validates it before making
changes. With no argument, extra arguments, or an unknown target, the script
prints usage and the available targets, exits with status `2`, and leaves the
existing build directory untouched.

Before cleaning, the scripts verify that Ninja and the tools required by the
selected firmware target are available. If a tool is not already on `PATH`, they
search `IDF_TOOLS_PATH` and standard per-user Espressif/EIM installation
locations and prepend the discovered executable directory to `PATH` for that
script run. ESP builds prefer the Espressif-managed `esptool` over an unrelated
system installation. Discovery does not permanently modify the user's shell or
PowerShell environment.

With a valid target, the script performs the equivalent of:

```sh
cmake -E remove_directory build
cmake --preset <target>
cmake --build --preset <target>
```

This is a destructive clean build: the first command deletes every existing
debug, release, and custom output under `build/`. The script then configures and
builds only the selected target. It does not run the tests; use
`ctest --preset <target>` after it finishes.

## Recommended preset workflow

CMake presets provide repeatable settings and keep generated files under
`build/`. List the presets known to the repository with:

```sh
cmake --list-presets=all
```

In that command:

- `cmake` runs the CMake command-line program.
- `--list-presets=all` lists configure, build, and test presets without
  configuring or building anything.

The host preset names are `host-debug` and `host-release`. A preset stores the
generator, output directory, build type, and project option values so they do
not have to be repeated on every command.

### Debug build

Configure the debug build:

```sh
cmake --preset host-debug
```

Here, `--preset host-debug` selects the `host-debug` configure preset from
`CMakePresets.json`. CMake reads the source tree, checks the compiler, resolves
the build settings, and writes Ninja's build instructions to
`build/host-debug`.

This creates `build/host-debug`, selects Ninja, enables host tests, emits debug
information, and requests address and undefined-behaviour sanitizers. The
sanitizer flags are applied when using GCC or Clang; unsupported compilers build
without them. Configuration only generates the build files; it does not
compile the project.

Compile it:

```sh
cmake --build --preset host-debug
```

Here, `--build` switches CMake from configuration mode to build-tool mode.
`--preset host-debug` selects the matching build preset and therefore the
already configured `build/host-debug` directory. With no `--target` argument,
Ninja builds its default target, which compiles the core library and test
executable.

Run its tests:

```sh
ctest --preset host-debug
```

Here, `ctest` runs tests registered by CMake and `--preset host-debug` selects
the test directory and output settings associated with the debug build. It
does not rebuild changed source files, so run the build command first.

CTest prints the output of any failing test. Leak detection is disabled for
sanitizer-enabled tests because it is not reliable in all traced or
sandboxed environments; address and undefined-behaviour checks remain enabled.

### Release build

Configure, compile, and test the optimized build:

```sh
cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

This creates `build/host-release`, enables compiler optimization, and leaves
sanitizers disabled. Running both debug and release tests checks the diagnostic
configuration as well as the configuration intended to represent normal
optimized library use.

### Build a specific target

Pass the target name after `--target`. For example, build only the core library
or the version test with:

```sh
cmake --build --preset host-debug --target microflow-core
cmake --build --preset host-debug --target microflow-version-test
```

`--target <name>` asks Ninja to build only the named target and anything that
target depends on. `microflow-core` produces the static core library;
`microflow-version-test` produces the test executable and also builds the core
library because the test links to it.

List all targets generated for a configured preset with:

```sh
cmake --build --preset host-debug --target help
```

The special Ninja `help` target prints available target names; it does not
compile them.

## Waveshare status-LED firmware

Configure and build one board at a time:

```sh
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug

cmake --preset raspberry-pi-pico-debug
cmake --build --preset raspberry-pi-pico-debug

cmake --preset waveshare-esp32-c6-zero-debug
cmake --build --preset waveshare-esp32-c6-zero-debug

cmake --preset waveshare-esp32-s3-zero-debug
cmake --build --preset waveshare-esp32-s3-zero-debug
```

The Pico blinks its single-colour onboard LED on GPIO25. Each Waveshare image
cycles its onboard WS2812 through dim red, green, blue, and off. The logical
status-LED calls are shared; the Waveshare boards select GPIO16, GPIO8, and
GPIO21 respectively. Both RP2040 presets produce ELF, map, BIN, and UF2 files.
Each ESP preset produces ELF, map, and Espressif BIN files.

The output files are under `build/<preset>/`. These initial firmware targets
are partial rather than supported: compilation and image conversion have been
verified, but the generated images have not yet passed on-device boot or LED
timing tests. Do not treat a successful build as hardware-conformance evidence.

For either RP2040 board, hold BOOTSEL/BOOT while connecting or resetting the
board, then copy its `microflow-*.uf2` file to the exposed mass-storage volume.

The ESP BIN files are standalone RAM-loaded images intended for the ROM boot
boundary. Flashing is deliberately not part of a normal build. After backing
up existing flash and placing the board in download mode, the initial test
commands are:

```sh
esptool --chip esp32c6 --port <port> write-flash 0x0 build/waveshare-esp32-c6-zero-debug/microflow-waveshare-esp32-c6-zero.bin
esptool --chip esp32s3 --port <port> write-flash 0x0 build/waveshare-esp32-s3-zero-debug/microflow-waveshare-esp32-s3-zero.bin
```

Writing at offset `0x0` replaces the existing ESP boot boundary. Restore the
backup or reflash the vendor/ESP-IDF bootloader and application to return the
board to its prior software. Hardware verification must confirm ROM loading,
CPU-clock assumptions, WS2812 timing, and reset behaviour before these targets
can be promoted beyond partial status.

## Raspberry Pi Pico 2 architecture builds

Pico 2 exposes separate RP2350 build compositions for its Cortex-M33 and
Hazard3 RISC-V cores:

```sh
./build.sh raspberry-pi-pico-2-arm-debug
./build.sh raspberry-pi-pico-2-riscv-debug
```

The Arm preset uses `arm-none-eabi-gcc`; the RISC-V preset uses
`riscv32-unknown-elf-gcc`. Each currently emits ELF, map, and BIN compile/link
artifacts. These are experimental architecture smoke tests, not flashable
firmware: RP2350 boot metadata, architecture-specific UF2 packaging, board
drivers, and hardware conformance are still required.

## Generated files and output locations

Each preset has a separate output tree:

```text
build/
  host-debug/
  host-release/
```

The debug and release directories use the same layout. The important generated
files are:

| Location relative to `build/<preset>/` | Purpose |
|---|---|
| `src/lib/core/libmicroflow-core.a` | Static core library produced by GCC, Clang, and Apple Clang. Microsoft Visual C++ produces the equivalent `.lib` file in this area. |
| `tests/unit/microflow-version-test` | Unit-test executable on Linux and macOS. Windows adds the `.exe` suffix. |
| `compile_commands.json` | Compiler command database used by editors and analysis tools. |
| `CMakeCache.txt` | Cached compiler paths and configured setting values. Inspect this when checking how a build tree was configured; do not edit it manually. |
| `build.ninja` | Ninja build instructions generated by CMake. |
| `CMakeFiles/` | CMake compiler checks, target metadata, and intermediate configuration files. |
| `src/lib/**/CMakeFiles/` and `tests/**/CMakeFiles/` | Object files and per-target intermediate build files. |
| `Testing/Temporary/LastTest.log` | Detailed log from the most recent CTest run. |

`microflow`, `microflow-blocks`, `microflow-runtime`, and `microflow-host` are
currently CMake interface targets. They express usage requirements and target
relationships, so they do not produce separate library files.

All files under `build/` are generated and ignored by Git. CMake, CTest, Ninja,
compiler output, and developer-local `CMakeUserPresets.json` are also covered
individually by `.gitignore` to prevent accidental commits from a non-standard
build location.

## Build settings

The presets set the options normally needed by contributors. The available
settings are:

| Setting | Values | Default | Purpose |
|---|---|---|---|
| `CMAKE_BUILD_TYPE` | `Debug`, `Release`, `RelWithDebInfo`, or `MinSizeRel` | Empty without a preset | Selects compiler optimization and debug-information policy for a single-configuration generator such as Ninja. The supplied presets use `Debug` and `Release`. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `ON` or `OFF` | `ON` in host presets | Writes `compile_commands.json` in the build directory for editors and static-analysis tools. It does not change generated code. |
| `MICROFLOW_BUILD_TESTS` | `ON` or `OFF` | `ON` | Adds the host support and test targets and enables CTest. Set it to `OFF` when only the libraries are required. |
| `MICROFLOW_ENABLE_SANITIZERS` | `ON` or `OFF` | `OFF` | Adds AddressSanitizer and UndefinedBehaviorSanitizer instrumentation when using GCC or Clang. |

All production and test targets use C11. GCC, Clang, and Microsoft Visual C++
builds enable the repository's strict warning set and treat warnings as errors.

To override a preset value for one build tree, pass a cache definition while
configuring. For example, create a debug build without sanitizers:

```sh
cmake --preset host-debug -DMICROFLOW_ENABLE_SANITIZERS=OFF
cmake --build --preset host-debug
ctest --preset host-debug
```

`-D<name>=<value>` creates or updates a CMake cache setting. In this example,
`-D` sets `MICROFLOW_ENABLE_SANITIZERS` to the Boolean value `OFF` while all
other values still come from the preset.

This changes the cached value in `build/host-debug`. Run the normal configure
command again with an explicit value to change it back, or fully clean that
build directory before reconfiguring.

## Manual configuration

Presets are recommended, but a separate custom build can be configured
directly:

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMICROFLOW_BUILD_TESTS=ON -DMICROFLOW_ENABLE_SANITIZERS=ON
cmake --build build/custom
ctest --test-dir build/custom --output-on-failure
```

The arguments mean:

- `-S .` selects the current repository directory as the source tree.
- `-B build/custom` selects `build/custom` as the generated output directory.
- `-G Ninja` selects Ninja as the underlying build tool.
- `-D...=...` sets the named CMake cache setting to the supplied value.
- `cmake --build build/custom` builds the default target in that explicit build
  directory instead of selecting a build preset.
- `ctest --test-dir build/custom` runs tests registered in that build directory.
- `--output-on-failure` prints a failing test's captured output to the terminal.

Keep custom builds under `build/` so they remain separate from source files and
are ignored by Git.

## Cleaning

To remove compiled outputs while retaining a preset's CMake configuration and
cache, run its generated `clean` target:

```sh
cmake --build --preset host-debug --target clean
cmake --build --preset host-release --target clean
```

For example, in the first command:

- `cmake --build` invokes the configured build tool through CMake.
- `--preset host-debug` selects `build/host-debug`.
- `--target clean` runs Ninja's generated `clean` target instead of the default
  compilation target.

The `clean` target removes compiled objects, libraries, and executables. It
retains configuration files such as `CMakeCache.txt`, `build.ninja`, and
`compile_commands.json`, as well as CTest logs. Use it when you want the next
build to recompile without repeating CMake configuration.

To completely reset one preset, remove only its build directory and configure
it again:

```sh
cmake -E remove_directory build/host-debug
cmake --preset host-debug
```

`-E` selects CMake's platform-independent utility-command mode;
`remove_directory` recursively deletes exactly the following directory. This
removes the preset's compiled outputs, test logs, generated Ninja files, and
CMake cache. The configure command then recreates them.

To completely reset all generated builds, remove the repository-local build
directory:

```sh
cmake -E remove_directory build
```

This uses the same utility mode to remove every preset and custom build stored
under `build/`.

These full-clean commands delete generated files, CMake caches, and compiled
outputs. They do not delete source files because all generated output is kept
under the explicitly named `build/` directory.

## Typical verification before submitting a change

See [TEST.md](TEST.md) for individual-test commands, sanitizer details,
supervisor-test coverage, firmware compile checks, and hardware-test evidence.

Run both supported configurations from a clean or already configured tree:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

A successful compile with no warnings and passing tests in both presets is the
minimum host-build check. Changes to portable behaviour should also add or
update the relevant host regression test.
