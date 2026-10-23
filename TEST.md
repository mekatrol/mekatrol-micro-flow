# Testing Mekatrol Micro Flow

## Purpose

This is the canonical guide for running and adding tests. For compiler setup,
see [PROJECT_SETUP.md](PROJECT_SETUP.md). For configuring firmware targets and
understanding generated artifacts, see [BUILD.md](BUILD.md).

Host tests are the primary regression suite for portable code and testable
board-owned policy. A successful firmware build is only a compile and link
check; it is not evidence that firmware boots or that hardware behaviour is
correct.

## Test inventory

The currently registered CTest cases are:

| CTest name | Source | Purpose |
|---|---|---|
| `microflow.version` | `tests/unit/version_test.c` | Checks the public version API. |
| `microflow.scheduler` | `tests/unit/scheduler_test.c` | Checks scheduling, priority, periodic timing, cancellation, capacity, and timer wraparound. |
| `microflow.supervisor` | `tests/unit/supervisor_test.c` | Checks the Waveshare RP2040 Zero supervisor, queues, overflow policy, service ordering and timing, heartbeat, idle waits, watchdog points, faults, wraparound, and repeated initialization. |

CTest names use dots. CMake executable target names use hyphens, such as
`microflow-supervisor-test`.

## Run the complete host suite

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer when
the compiler supports them:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

The release preset tests optimized code without sanitizers:

```sh
cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

Always build before invoking CTest. CTest runs the binaries already present in
the build directory; it does not automatically rebuild changed source files.
Both command sequences work in PowerShell, Command Prompt, and POSIX shells.

To print every test's output, including successful tests, add `--verbose`:

```sh
ctest --preset host-debug --verbose
```

To show the registered tests without running them:

```sh
ctest --preset host-debug --show-only
```

## Run one test

Use `-R` with an anchored regular expression to select one CTest case:

```sh
ctest --preset host-debug -R '^microflow\.supervisor$'
ctest --preset host-debug -R '^microflow\.scheduler$'
ctest --preset host-debug -R '^microflow\.version$'
```

Anchoring with `^` and `$` prevents a partial name from selecting additional
tests. The backslash makes the dot literal instead of the regular-expression
“any character” operator.

To rerun only cases that failed in the preceding CTest run:

```sh
ctest --preset host-debug --rerun-failed --output-on-failure
```

To repeat a case and expose intermittent failures:

```sh
ctest --preset host-debug -R '^microflow\.supervisor$' --repeat until-fail:100
```

## Build or execute one test binary

Build only the selected executable and its dependencies:

```sh
cmake --build --preset host-debug --target microflow-supervisor-test
```

CTest is preferred because it applies the test environment configured by
CMake. For debugger use or a quick local run, the executable can also be
started directly after it has been built:

```sh
./build/host-debug/tests/unit/microflow-supervisor-test
```

On Windows, use:

```powershell
.\build\host-debug\tests\unit\microflow-supervisor-test.exe
```

These small test programs normally produce no output and return exit status
zero. A failed `CHECK` calls `abort()`, producing a nonzero result. When run
through CTest, use `--output-on-failure` or inspect
`build/host-debug/Testing/Temporary/LastTest.log` for captured output.

## Supervisor test details

`tests/unit/supervisor_test.c` compiles the production board-owned
`src/boards/rpi/waveshare-rp2040-zero/supervisor.c` into a host executable. It
does not emulate RP2040 registers or execute the interrupt assembly.

Instead, a fixture supplies the supervisor platform callbacks:

- a manually controlled 32-bit clock;
- an idle-wait recorder;
- watchdog and fault counters; and
- service callbacks that record dispatch order and advance simulated time.

This arrangement makes timing, overflow, wraparound, and warm-reset state
deterministic and allows the production supervisor code to run under host
sanitizers. RP2040 vector dispatch, Timer Alarm 0, `WFI`, watchdog registers,
and real reset behaviour still require firmware and physical-board checks.

## Sanitizers

`host-debug` requests AddressSanitizer and UndefinedBehaviorSanitizer for GCC
and Clang. AddressSanitizer detects errors such as out-of-bounds and use-after-
free accesses. UndefinedBehaviorSanitizer detects operations such as invalid
shifts and signed overflow.

Leak detection is disabled for the registered tests because it is unreliable
in some traced and sandboxed environments. The code remains instrumented for
address and undefined-behaviour failures.

To create a debug build without sanitizer instrumentation:

```sh
cmake --preset host-debug -DMICROFLOW_ENABLE_SANITIZERS=OFF
cmake --build --preset host-debug
ctest --preset host-debug
```

This modifies the cached setting in `build/host-debug`. Reconfigure with
`-DMICROFLOW_ENABLE_SANITIZERS=ON`, or remove that build directory, to restore
the preset's normal setting.

## Firmware compile checks

When a change affects the Waveshare RP2040 Zero integration, also build its
explicit board preset:

```sh
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

This checks the Arm instruction set, freestanding compilation, startup/vector
symbols, linker layout, and ELF/BIN/UF2 generation. It does not run the host
tests and does not exercise physical hardware.

The Stage 1 supervisor requires on-device validation of at least:

- cold boot and repeated warm resets;
- extended idle and active runtime without watchdog starvation;
- Timer Alarm 0 wake-up and 32-bit timer wraparound;
- observable queue overflow without corruption or deadlock; and
- actual service runtime and queue high-water measurements.

Record the board revision, firmware commit, toolchain version, test duration,
reset count, observed measurements, and result when collecting hardware
evidence. Do not promote a target based only on a successful cross-build.

## Add a unit test

Place host unit tests under `tests/unit/`. A typical test executable is declared
in `tests/unit/CMakeLists.txt`:

```cmake
add_executable(microflow-example-test example_test.c)
target_link_libraries(microflow-example-test PRIVATE Microflow::microflow)

microflow_set_warnings(microflow-example-test)
microflow_enable_sanitizers(microflow-example-test)

add_test(NAME microflow.example COMMAND microflow-example-test)
```

Also add the new CTest name to the sanitizer test-property list at the bottom
of that file. Tests must remain deterministic, avoid external services, and
return nonzero on failure. Portable bug fixes require a host regression test.

After changing CMake files, configure again before building:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

## Custom build directory

Tests can be configured without presets when necessary:

```sh
cmake -S . -B build/custom -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMICROFLOW_BUILD_TESTS=ON -DMICROFLOW_ENABLE_SANITIZERS=ON
cmake --build build/custom
ctest --test-dir build/custom --output-on-failure
```

Run a single custom-build test with:

```sh
ctest --test-dir build/custom -R '^microflow\.supervisor$' --output-on-failure
```

Keep custom outputs beneath `build/` so they remain ignored and separate from
source files.

## Minimum verification before submitting

For portable or supervisor changes, run:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug

cmake --preset host-release
cmake --build --preset host-release
ctest --preset host-release
```

Also build every affected firmware preset. Run the applicable physical
hardware tests for driver, startup, interrupt, timing, reset, or board changes,
and report separately which results came from host tests, cross-builds, and
real hardware.
