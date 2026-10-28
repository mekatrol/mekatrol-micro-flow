# CH32V003 Hardware Debugging Guide

## 1. Purpose

This document describes the Linux hardware debugging setup for the CH32V003F4P6 RS-485 Output Expander C firmware.

It covers:

- Installing the WCH-compatible OpenOCD debugger.
- Building OpenOCD from source.
- Configuring the WCH-LinkE adapter and CH32V003 target.
- Building and flashing debug firmware.
- Starting OpenOCD and GDB in separate terminals.
- Inspecting CPU registers, memory and C variables.
- Setting breakpoints and stepping through source code.
- Diagnosing RS-485 reception.
- Exiting GDB and OpenOCD safely.
- Avoiding firmware/ELF mismatches.
- Troubleshooting common problems.

The debugging setup has been tested on Linux with a WCH-LinkE connected to a CH32V003F4P6.

**Important:** The standard Ubuntu OpenOCD package does not include the WCH-LinkE adapter driver required by this setup. A separate WCH-specific build is used.

---

## 2. Verified development environment

| Component | Version or configuration |
|---|---|
| Target | CH32V003F4P6 |
| Debug adapter | WCH-LinkE, RV firmware 2.10 |
| Debug connection | PD1/SWIO |
| System clock | 48 MHz nominal |
| Cross-compiler | xPack RISC-V GCC 14.3.0-1 |
| GDB | xPack RISC-V GDB 16.3 |
| WCH OpenOCD | 0.11.0+dev-gccb04d7-dirty |
| OpenOCD source | `hathach/riscv-openocd-wch` |
| OpenOCD GDB port | TCP 3333 |
| Host operating system | Linux x86-64 |
| Build system | CMake and Ninja |

The source and installation directories used in this setup are:

```text
~/opt/riscv-openocd-wch/
~/opt/wch-openocd/
~/opt/xpack-riscv-none-elf-gcc-14.3.0-1/
```

Project directories:

```text
output-expander/
├── debug/
│   └── openocd.cfg
├── docs/
│   ├── DEBUG.md
│   └── PROJECT_SETUP.md
├── tools/
│   ├── build.sh
│   └── flash.sh
├── src/
└── build/
    ├── ch32v003-debug/
    │   ├── output-expander.elf
    │   └── output-expander.bin
    └── ch32v003-release/
        ├── output-expander.elf
        └── output-expander.bin
```

The `build/` directories are generated and must not be committed to Git.

### Source revision tracking

Record the full OpenOCD source commit used for the installation.

From a Linux shell:

```bash
cd "$HOME/opt/riscv-openocd-wch"
```

Then:

```bash
git rev-parse HEAD
```

Also inspect the working tree:

```bash
git status --short
```

The output of `git rev-parse HEAD` should be copied into the project dependency inventory, along with any local modifications.

The `-dirty` suffix in the OpenOCD version indicates that its build identifies the source tree as modified. The abbreviated commit alone is not a complete reproducibility record.

---

## 3. Hardware connections

The CH32V003F4P6 uses WCH's single-wire SDI debugging interface.

| WCH-LinkE | CH32V003F4P6 |
|---|---|
| SWIO / SWDIO | PD1 / SWIO |
| GND | GND |
| 3V3 power output | Not connected; board separately powered |

The target board is powered from its normal power supply.

Do not connect the WCH-LinkE power output to an independently powered target unless the hardware design explicitly allows it.

The existing RS-485 connections are unchanged:

| Function | MCU pin |
|---|---|
| USART1 TX | PD5 |
| USART1 RX | PD6 |
| RS-485 DE and /RE | PC6 |

The eight active-low output connections also remain unchanged.

**A debugger can halt the processor while outputs retain their previous electrical states.** Perform initial debugging with all outputs OFF and with external loads in a safe condition.

---

# Part A — One-time OpenOCD installation

## 4. Install build prerequisites

These instructions are for Ubuntu or a compatible Debian-derived Linux distribution.

Open a normal terminal.

Run:

```bash
sudo apt update
```

Then:

```bash
sudo apt install \
  git \
  build-essential \
  autoconf \
  automake \
  libtool \
  pkg-config \
  texinfo \
  libusb-1.0-0-dev \
  libhidapi-dev \
  libftdi1-dev
```

These packages are needed to compile the host-side OpenOCD executable.

They do not replace the project's pinned xPack RISC-V compiler.

Do not use `sudo` when subsequently compiling or installing OpenOCD into directories owned by the current user.

## 5. Clone WCH-compatible OpenOCD

The source repository is:

https://github.com/hathach/riscv-openocd-wch

This repository provides a WCH-LinkE adapter driver not available in the standard Ubuntu OpenOCD package.

Open a Linux terminal.

Create the tools directory:

```bash
mkdir -p "$HOME/opt"
```

Change directory:

```bash
cd "$HOME/opt"
```

Clone the repository:

```bash
git clone --recursive https://github.com/hathach/riscv-openocd-wch.git
```

Enter the source directory:

```bash
cd "$HOME/opt/riscv-openocd-wch"
```

Record the source revision:

```bash
git rev-parse HEAD
```

For a reproducible installation, preserve this exact revision in `docs/DEPENDENCIES.md`. Do not rely on an unpinned latest branch for future rebuilds.

## 6. Configure OpenOCD

Remain in:

```text
~/opt/riscv-openocd-wch
```

Run the bootstrap script:

```bash
./bootstrap
```

Then configure the build:

```bash
./configure \
  --prefix="$HOME/opt/wch-openocd" \
  --disable-werror \
  --enable-wlinke \
  --disable-ch347
```

Explanation:

| Option | Purpose |
|---|---|
| `--prefix` | Install OpenOCD in a private tools directory |
| `--disable-werror` | Do not fail the third-party build solely because of warnings |
| `--enable-wlinke` | Include the required WCH-LinkE adapter driver |
| `--disable-ch347` | Disable the unneeded CH347 adapter driver |

This installation does not replace `/usr/bin/openocd`.

## 7. Compile and install

From the same directory:

```bash
make -j"$(nproc)"
```

Then:

```bash
make install
```

The executable should now exist at:

```text
~/opt/wch-openocd/bin/openocd
```

Verify:

```bash
"$HOME/opt/wch-openocd/bin/openocd" --version
```

The successfully tested installation reported:

```text
Open On-Chip Debugger 0.11.0+dev-gccb04d7-dirty
```

Check which adapter drivers were compiled:

```bash
"$HOME/opt/wch-openocd/bin/openocd" \
  -c "adapter list" \
  -c shutdown
```

The output must include:

```text
wlinke
```

If `wlinke` is absent, this OpenOCD executable cannot use the required adapter.

### Important: multiple OpenOCD installations

This development computer also has other OpenOCD installations, including:

```text
/usr/bin/openocd
~/.pico-sdk/openocd/0.12.0+dev/openocd
```

Neither was found to provide the WCH-LinkE driver.

Always use the explicit path:

```bash
"$HOME/opt/wch-openocd/bin/openocd"
```

Do not assume that typing `openocd` selects the correct executable.

---

# Part B — OpenOCD configuration

## 8. Create the configuration directory

Open a terminal and navigate to the C project root:

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

Create the directory if necessary:

```bash
mkdir -p debug
```

Create:

```text
debug/openocd.cfg
```

Use this configuration:

```tcl
# CH32V003F4P6 using WCH-LinkE.
#
# Board powered separately.
# WCH-LinkE SWIO connected to PD1.
# Common ground required.
#
# Requires WCH-modified OpenOCD with wlinke support.

# Select the WCH-LinkE probe.
adapter driver wlinke

# Start at 400 kHz for a conservative debug connection.
adapter speed 400

# The WCH fork automatically selects its SDI transport.
# No explicit transport select command is required.

# WCH flash address mapping.
wlink_set_address 0x00000000

# Define the RISC-V debug interface.
set _CHIPNAME wch_riscv
sdi newtap $_CHIPNAME cpu -irlen 5 -expected-id 0x00001

# Create the CPU target.
set _TARGETNAME $_CHIPNAME.cpu
target create $_TARGETNAME.0 wch_riscv -chain-position $_TARGETNAME

# Configure the WCH flash driver.
set _FLASHNAME $_CHIPNAME.flash
flash bank $_FLASHNAME wch_riscv 0x00000000 0 0 0 $_TARGETNAME.0
```

This configuration is based on the settings used for the successfully connected CH32V003.

The `transport select sdi` command is intentionally omitted. In the tested installation, the transport was automatically selected, and specifying it explicitly caused a redundant-selection warning.

## 9. Verify target connection

Connect the WCH-LinkE to the target and power the board.

Ensure no other program is using the WCH-LinkE.

From the project root, run:

```bash
"$HOME/opt/wch-openocd/bin/openocd" \
  -f debug/openocd.cfg
```

The successful hardware session reported:

```text
Info : WCH-LinkE  mode:RV version 2.10
Info : wlink_init ok
Info : clock speed 400 kHz
Info : [wch_riscv.cpu.0] datacount=2 progbufsize=8
Info : [wch_riscv.cpu.0] Examined RISC-V core; found 1 harts
Info : [wch_riscv.cpu.0] XLEN=32, misa=0x40800014
[wch_riscv.cpu.0] Target successfully examined.
Info : starting gdb server for wch_riscv.cpu.0 on 3333
Info : Listening on port 3333 for gdb connections
```

This confirms:

- The WCH-LinkE was detected.
- The CH32V003 debug interface responded.
- The RISC-V processor was examined.
- The GDB server started.

**Leave this terminal running.**

OpenOCD does not return to the shell prompt while serving GDB connections. This is normal.

---

# Part C — Build and flash matching debug firmware

## 10. Why matching firmware matters

The project has separate debug and release configurations.

The builds use different optimization settings and produce different machine-code layouts.

A successful build produced approximately:

| Build | Flash usage |
|---|---:|
| Release | 2,856 bytes |
| Debug | 4,400 bytes |

These are historical measurements, not permanent limits.

When debugging, there are two distinct things:

1. The actual program stored in the CH32V003 Flash.
2. The ELF file loaded into GDB, which supplies symbols and source-code information.

**Loading an ELF into GDB does not automatically program the microcontroller.**

If GDB loads symbols from a debug build but the device runs a release build, the displayed variable addresses and source locations may be incorrect.

This problem was suspected during the initial debugging session when a diagnostic packet counter appeared to contain unexpectedly large values.

Always flash the exact debug build before relying on source-level information.

## 11. Build debug firmware

Open a normal Linux shell in the C project directory.

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

Build:

```bash
./tools/build.sh ch32v003-debug
```

Confirm the ELF exists:

```bash
ls -lh build/ch32v003-debug/output-expander.elf
```

Confirm the binary exists:

```bash
ls -lh build/ch32v003-debug/output-expander.bin
```

Inspect memory usage:

```bash
riscv-none-elf-size build/ch32v003-debug/output-expander.elf
```

The target architecture remains:

```text
-march=rv32ec_zicsr
-mabi=ilp32e
```

The debug preset should retain debugging information and suitable optimization settings, normally `-Og -g3`.

These are properties of the build configuration; inspect the actual compile commands if necessary.

## 12. Flash the debug firmware

**Before flashing, disconnect GDB and stop OpenOCD.**

The existing project flash script from the original source archive supports selecting the debug or release build:

```bash
./tools/flash.sh ch32v003-debug
```

It rebuilds the selected configuration and uses `wlink` to program the corresponding ELF.

Confirm that your current `tools/flash.sh` still supports this argument.

Do not accidentally run:

```bash
./tools/flash.sh
```

when you intend to flash the debug build. In the original script, omitting the argument selects `ch32v003-release`.

After flashing, avoid rebuilding or changing the ELF before starting GDB. Otherwise the GDB symbols might no longer correspond to the image just programmed.

### Optional: verify build identity

You can record the SHA-256 digest of the debug ELF:

```bash
sha256sum build/ch32v003-debug/output-expander.elf
```

This records the host artifact's identity. It does not independently verify the bytes in MCU Flash.

The existing `wlink` flashing command reports programming results; a separate Flash readback or verify procedure would be required for independent byte-for-byte confirmation.

---

# Part D — Day-to-day debugging workflow

## 13. Terminal arrangement

Use two separate terminal windows:

| Terminal | Program | Purpose |
|---|---|---|
| Terminal 1 | OpenOCD | WCH-LinkE communication and GDB server |
| Terminal 2 | RISC-V GDB | Breakpoints, registers, variables, execution control |

A third terminal is useful when running Python RS-485 hardware tests.

**Commands in a Linux shell and commands inside GDB are different. Do not paste GDB commands at the Bash prompt or Bash commands into GDB.**

The sections below explicitly separate them.

## 14. Terminal 1 — Start OpenOCD

Open the first terminal.

At the normal Linux Bash prompt, enter:

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

Then:

```bash
"$HOME/opt/wch-openocd/bin/openocd" \
  -f debug/openocd.cfg
```

Wait until OpenOCD reports:

```text
Listening on port 3333 for gdb connections
```

Leave this terminal open.

Do not type the following GDB commands into this terminal.

## 15. Terminal 2 — Launch GDB

Open another terminal window.

At the Bash prompt:

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

Launch GDB:

```bash
riscv-none-elf-gdb build/ch32v003-debug/output-expander.elf
```

You should reach:

```text
(gdb)
```

Everything in the next section is entered **inside GDB**, not Bash.

## 16. Connect to OpenOCD from GDB

Enter these commands **one at a time**.

First, disable pagination:

```gdb
set pagination off
```

This prevents the `--Type <RET> for more--` prompt from interrupting register and variable dumps.

Connect to the running OpenOCD server:

```gdb
target remote localhost:3333
```

A successful connection may report a source location such as:

```text
0x00000690 in hal_init (...) at src/hal.c:117
```

The location depends on the target's current execution state.

Halt the CPU if necessary:

```gdb
monitor halt
```

Inspect the current program counter:

```gdb
print/x $pc
```

Inspect the stack pointer:

```gdb
print/x $sp
```

Inspect all registers:

```gdb
info registers
```

The exact register contents vary.

### Important GDB input rule

Run commands separately.

Correct:

```gdb
print debug_valid_packet_count
```

Then, after GDB prints the result:

```gdb
print/x debug_last_sequence
```

Do not paste two commands as one expression:

```text
print debug_valid_packet_count print/x debug_last_sequence
```

That produces a GDB syntax error.

---

# Part E — Inspecting the processor

## 17. General register inspection

While halted, enter:

```gdb
info registers
```

Useful registers include:

| Register | Meaning |
|---|---|
| `pc` | Program counter, address of the current instruction |
| `sp` | Stack pointer, location of the active stack |
| `ra` | Return-address register |
| `a0`–`a5` | Common argument and return-value registers |
| `s0` / `fp`, `s1` | Saved registers, including a frame pointer when used |
| `t0`–`t2` | Temporary registers |

The CH32V003 uses the RV32E embedded register profile, with 16 general-purpose integer registers rather than the 32 of a full RV32I implementation.

A remote debugger may nevertheless display additional register names. Their appearance in GDB does not prove those registers physically exist in the RV32E core.

To inspect one register:

```gdb
print/x $pc
```

Or:

```gdb
print/x $sp
```

To inspect surrounding machine instructions:

```gdb
x/10i $pc
```

To see the current C source location:

```gdb
list
```

To display the current stack backtrace:

```gdb
backtrace
```

The backtrace may be limited by compiler optimization, assembly wrappers or incomplete unwind information.

## 18. Inspecting raw memory

GDB supports examining target memory directly.

To display eight 32-bit words starting at the current stack pointer:

```gdb
x/8wx $sp
```

To inspect 16 bytes:

```gdb
x/16xb $sp
```

To inspect instructions:

```gdb
x/12i $pc
```

Format characters:

| Format | Meaning |
|---|---|
| `x` | Hexadecimal |
| `d` | Decimal |
| `b` | Byte-sized unit |
| `h` | Halfword-sized unit |
| `w` | Word-sized unit |
| `i` | Machine instruction |

For example, `x/8wx` means eight hexadecimal 32-bit words.

Use only known valid memory addresses when entering explicit addresses. Invalid or peripheral addresses can result in target access errors or hardware side effects.

---

# Part F — Inspecting firmware variables

## 19. Diagnostic variables

The Step 2.12 firmware introduced:

```c
volatile uint32_t debug_valid_packet_count;
volatile uint8_t debug_last_destination;
volatile uint8_t debug_last_sequence;
volatile uint8_t debug_last_type;
volatile uint8_t debug_last_command;
volatile uint8_t debug_last_payload_length;
```

These variables are intended to expose the received packet metadata without sending unsolicited responses.

While the processor is halted, enter each of the following commands separately.

```gdb
print debug_valid_packet_count
```

```gdb
print/x debug_last_destination
```

```gdb
print/x debug_last_sequence
```

```gdb
print/x debug_last_type
```

```gdb
print/x debug_last_command
```

```gdb
print debug_last_payload_length
```

Expected values after a correctly received PING request with destination `0xFF` and sequence `0x42`:

| Variable | Expected value |
|---|---|
| `debug_valid_packet_count` | Increases by one |
| `debug_last_destination` | `0xFF` |
| `debug_last_sequence` | `0x42` |
| `debug_last_type` | `0x00` |
| `debug_last_command` | `0x01` |
| `debug_last_payload_length` | `0` |

The count may differ if other valid frames have already been received.

If GDB displays unexpectedly large values, do not immediately assume memory corruption. First verify that the flashed firmware matches the debug ELF.

## 20. Inspect variable addresses

To display the location of a global variable:

```gdb
print/x &debug_valid_packet_count
```

To display its raw memory:

```gdb
x/wx &debug_valid_packet_count
```

To inspect its C type:

```gdb
ptype debug_valid_packet_count
```

To inspect a local variable, execution must be stopped in a scope where that variable exists and its location is available:

```gdb
info locals
```

For example, when stopped in the foreground packet-processing loop:

```gdb
print packet
```

A packet's individual fields may be inspected:

```gdb
print/x packet.destination
```

```gdb
print/x packet.sequence
```

```gdb
print/x packet.command
```

GDB may report that a local variable is unavailable or optimized out if the compiler did not retain a separately inspectable value.

---

# Part G — Breakpoints and stepping

## 21. Set a breakpoint

Breakpoints stop execution when the processor reaches a selected location.

While the processor is halted, try:

```gdb
break main
```

To inspect current breakpoints:

```gdb
info breakpoints
```

To resume execution:

```gdb
continue
```

If execution reaches the breakpoint, GDB returns to its prompt.

**Important:** If the processor has already passed the start of `main()`, a breakpoint there will not be reached until execution returns there, typically after a reset.

WCH-LinkE/OpenOCD breakpoint support and the number of usable hardware breakpoints should be verified on this specific combination before relying on multiple simultaneous breakpoints.

## 22. Set a breakpoint in protocol decoding

While halted:

```gdb
break protocol_decode
```

Then:

```gdb
continue
```

Send a valid COBS frame over RS-485 while the processor is running.

When the breakpoint triggers, inspect:

```gdb
backtrace
```

Then:

```gdb
info args
```

Then:

```gdb
info locals
```

To proceed:

```gdb
continue
```

If ordinary `break` fails because the target cannot insert a breakpoint, inspect the OpenOCD/GDB error. A hardware breakpoint may be attempted using `hbreak`, subject to the target's available trigger resources.

## 23. Stepping

While stopped at a breakpoint:

Step to the next source line, entering called functions:

```gdb
step
```

Step to the next source line without entering called functions:

```gdb
next
```

Execute one machine instruction:

```gdb
stepi
```

Execute one machine instruction, stepping over calls where supported:

```gdb
nexti
```

Continue until the current function returns:

```gdb
finish
```

Resume normal execution:

```gdb
continue
```

Not every source line necessarily corresponds to a distinct instruction. Optimization and inlining can make stepping appear to skip or revisit C lines.

---

# Part H — Testing RS-485 reception

## 24. Important interrupt behavior

USART1 reception uses interrupts.

The ISR places received bytes and UART errors into a ring buffer. The foreground runs `frame_receiver_poll()` to assemble COBS frames and validate protocol packets.

The protocol parser does not run inside the ISR.

When the processor is halted, normal interrupt processing and foreground consumption may stop.

Therefore:

- Do not send sustained serial traffic while the CPU is halted.
- Do not use breakpoint-heavy sessions to measure receive throughput.
- For ordinary packet-counting tests, keep the CPU running while transmitting.
- Halt the CPU only after the traffic has finished, then inspect variables.

## 25. Manual packet reception test

This procedure assumes the matching debug ELF has been flashed.

### Terminal 1 — OpenOCD

Start OpenOCD as described earlier.

### Terminal 2 — GDB

Connect GDB.

While halted, inspect the current packet count:

```gdb
print debug_valid_packet_count
```

Record the value.

Resume:

```gdb
continue
```

GDB will now display `Continuing.` and normally will not show another prompt until execution stops.

### Terminal 3 — Python RS-485 test

Open a third Linux terminal.

Navigate to the C project:

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

Activate the **C project's** virtual environment:

```bash
source .venv/bin/activate
```

Run the hardware test script:

```bash
python tools/test_rx_hardware.py --port /dev/ttyACM1
```

This script sends valid and deliberately invalid frames.

The Python script's completion alone does not prove that the MCU received them. The debug variables provide the evidence.

### Terminal 2 — Return to GDB

After traffic has finished, return to the GDB terminal.

Press **Ctrl+C**.

GDB should report that the target stopped.

Now inspect:

```gdb
print debug_valid_packet_count
```

Then:

```gdb
print/x debug_last_sequence
```

For the full Step 2.13 test sequence, assuming no other valid packets were received, the expected count increase is eight, and the last valid sequence is `0x61`.

If the diagnostic count increases but the values are unexpected, inspect the full metadata and the exact flashed firmware.

---

# Part I — Resetting and restarting

## 26. Halting

To halt execution from GDB:

```gdb
monitor halt
```

This is meaningful when GDB has an interactive prompt. When the inferior is running under `continue`, use Ctrl+C to interrupt it first.

## 27. Resetting

OpenOCD commonly supports reset commands such as:

```gdb
monitor reset halt
```

This requests a target reset followed by a halt.

The command has not yet been confirmed on the specific WCH OpenOCD fork used in this project.

If it fails, record the error and use the existing `wlink` reset mechanism only after disconnecting OpenOCD.

A reset restarts the firmware initialization sequence and resets ordinary static diagnostic variables to their startup values.

## 28. Restarting from `main()`

A typical debugging sequence after a supported reset is:

```gdb
monitor reset halt
```

Then:

```gdb
break main
```

Then:

```gdb
continue
```

Once the breakpoint is reached:

```gdb
print debug_valid_packet_count
```

Do not expect an already-running program to return to `main()` just because a breakpoint was created.

---

# Part J — Exiting safely

## 29. Stop running execution

If GDB currently shows:

```text
Continuing.
```

press **Ctrl+C** in the GDB terminal.

Wait for:

```text
(gdb)
```

Do not type `q` or `quit` while the inferior is running and expect it to be interpreted as a GDB command.

## 30. Detach GDB

At the GDB prompt:

```gdb
detach
```

Detaching disconnects GDB from the remote target. The target's subsequent running or halted state can depend on the remote server and target implementation.

If you need the application to be running before detaching, explicitly resume and verify its state as supported by the debugger.

## 31. Quit GDB

At the GDB prompt:

```gdb
quit
```

This returns to the normal Bash shell.

Do not enter `quit` at an OpenOCD console expecting it to exit GDB.

## 32. Stop OpenOCD

Switch to Terminal 1, which is running OpenOCD.

Press **Ctrl+C**.

The OpenOCD process should stop and the terminal should return to the shell prompt.

The WCH-LinkE should then be available to the normal `wlink` flashing command.

Only one program should own the probe at a time.

---

# Part K — Troubleshooting

## 33. `No such file or directory` for OpenOCD

Check:

```bash
ls -l "$HOME/opt/wch-openocd/bin/openocd"
```

If missing, review the OpenOCD build and installation steps.

Check the installed adapter drivers:

```bash
"$HOME/opt/wch-openocd/bin/openocd" \
  -c "adapter list" \
  -c shutdown
```

The output must contain `wlinke`.

## 34. OpenOCD cannot access the WCH-LinkE

First check whether the adapter is visible:

```bash
wlink list
```

Run this only when OpenOCD and GDB are stopped.

Check USB detection:

```bash
lsusb
```

Confirm USB device permissions, target power, and wiring.

Do not run OpenOCD and `wlink flash` simultaneously.

## 35. GDB cannot connect to port 3333

Make sure Terminal 1 reports:

```text
Listening on port 3333 for gdb connections
```

Check whether the port is listening:

```bash
ss -ltn | grep ':3333'
```

Then, in GDB:

```gdb
target remote localhost:3333
```

If the connection fails, inspect the OpenOCD terminal for target or USB errors.

## 36. GDB displays incorrect source locations

Symptoms include:

- Execution appearing to jump unexpectedly between unrelated functions.
- Local variable values that contradict the known calling code.
- Breakpoints appearing at unexpected instructions.
- Unexpectedly large global counters.

First verify that the debug build was flashed to the microcontroller.

Correct sequence:

1. Stop and disconnect GDB.
2. Stop OpenOCD.
3. Build `ch32v003-debug`.
4. Flash `ch32v003-debug`.
5. Start OpenOCD.
6. Start GDB with that same debug ELF.
7. Connect and inspect.

Do not assume a matching firmware image merely because GDB accepts the ELF file.

If problems persist after matching the firmware, investigate resets, stack corruption, incorrect debugging metadata or interrupt behavior.

## 37. The program unexpectedly returns to `hal_init()`

Possible causes include:

- A hardware or software reset.
- Incorrect debug symbols.
- An exception or fault leading to a restart.
- Startup or interrupt-wrapper problems.

Verify the flashed ELF first.

Then inspect the PC, stack and relevant startup/interrupt code.

Do not modify the verified AHB clock prescaler correction solely because GDB reports a surprising source line.

## 38. GDB displays `--Type <RET> for more--`

Disable pagination:

```gdb
set pagination off
```

Run this at the beginning of each new GDB session.

## 39. GDB reports a syntax error while printing variables

Each GDB command should be entered separately.

Example:

```gdb
print debug_valid_packet_count
```

Wait for the result.

Then:

```gdb
print/x debug_last_sequence
```

Do not combine the two commands on one input line.

## 40. The CPU stops responding during serial testing

A halted CPU may not service incoming interrupts.

Stop sending traffic, resume execution, and try again.

The existing RX queue is small. It is designed for continuous foreground consumption, not for accumulating sustained traffic while the CPU is halted.

---

# Part L — Quick reference

## 41. Fresh debug session

Use this sequence when the correct debug firmware has already been flashed.

### Terminal 1 — Linux shell

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

```bash
"$HOME/opt/wch-openocd/bin/openocd" -f debug/openocd.cfg
```

Leave Terminal 1 running.

### Terminal 2 — Linux shell

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

```bash
riscv-none-elf-gdb build/ch32v003-debug/output-expander.elf
```

### Terminal 2 — Inside GDB

Enter each separately:

```gdb
set pagination off
```

```gdb
target remote localhost:3333
```

```gdb
monitor halt
```

```gdb
info registers
```

```gdb
print debug_valid_packet_count
```

```gdb
continue
```

### Stop execution

Press Ctrl+C in the GDB terminal.

### Inspect again

```gdb
print debug_valid_packet_count
```

### Exit GDB

```gdb
detach
```

```gdb
quit
```

### Exit OpenOCD

Switch to Terminal 1 and press Ctrl+C.

## 42. Rebuild and reflash debug firmware

With GDB and OpenOCD both stopped:

```bash
cd "$HOME/repos/mekatrol-micro-flow/output-expander"
```

```bash
./tools/build.sh ch32v003-debug
```

```bash
./tools/flash.sh ch32v003-debug
```

Then start a fresh debugging session using the previous section.

## 43. Release firmware restoration

After debugging, stop GDB and OpenOCD.

Build the release configuration:

```bash
./tools/build.sh ch32v003-release
```

Flash it:

```bash
./tools/flash.sh ch32v003-release
```

The release firmware should not include temporary diagnostic RS-485 responses.

---

# Part M — Current verification status

The following debugging operations were confirmed on the physical CH32V003:

| Operation | Status |
|---|---|
| Build WCH OpenOCD | Passed |
| Compile `wlinke` adapter driver | Passed |
| WCH-LinkE detected | Passed |
| CH32V003 target examined | Passed |
| GDB server listening on port 3333 | Passed |
| GDB remote connection | Passed |
| CPU register inspection | Passed |
| Halt execution | Passed |
| Resume execution | Passed |
| Interrupt execution with Ctrl+C | Passed |
| Resolve program locations using ELF symbols | Passed |
| Debug build compilation | Passed |
| Flash matching debug image | Pending explicit confirmation |
| Reliable diagnostic-variable inspection | Pending matching ELF verification |
| Source breakpoints | Pending hardware test |
| Reset/halt through OpenOCD | Pending hardware test |
| Step through protocol decoding | Pending hardware test |

The debugging environment is sufficiently operational to proceed with validating the matching debug firmware.

The next firmware validation task is to flash `ch32v003-debug`, reconnect GDB, and confirm the interrupt-driven receive counter increases by the expected number of packets.

---

## Related documentation

- `PROJECT_SETUP.md` — project toolchains and build environment.
- `BUILD.md` — build and flashing procedures.
- `DEPENDENCIES.md` — version-pinned tool and library dependencies.
- `protocol.md` — RS-485 packet format and protocol rules.
- `hardware-validation.md` — hardware verification history.