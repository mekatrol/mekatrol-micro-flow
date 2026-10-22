# Project setup

This guide prepares a clean Windows, macOS, or Linux machine to build and test
Mekatrol Micro Flow. Install the common tools first, then install only the
cross-compilers required for the boards you intend to build.

The project does not link Raspberry Pi Pico SDK or ESP-IDF runtime libraries
into firmware. Their compiler distributions and host-side image tools are used
only to compile and package the project-owned bare-metal code.

## Tool summary

| Purpose | Required tool | Minimum or pinned version |
|---|---|---|
| Source control | Git | Current supported release |
| Configure and test | CMake and CTest | 3.25 or newer |
| Build backend | Ninja | Current supported release |
| Host build | C11 compiler | Current GCC, Clang, Apple Clang, or MSVC |
| Firmware helper scripts | Python | 3.9 or newer |
| RP2040 firmware | Arm GNU Toolchain | 13.2.Rel1, `arm-none-eabi` target |
| ESP32-C6 firmware | Espressif GCC | 15.2.0_20251204, `riscv32-esp-elf` target |
| ESP32-S3 firmware | Espressif GCC | 15.2.0_20251204, `xtensa-esp32s3-elf` target |
| ESP image creation and flashing | esptool | 5.1.0 |

Arm GNU Toolchain 13.2.Rel1, Espressif GCC 15.2.0_20251204, and esptool
5.1.0 are the versions used for the initial verified builds. Do not silently
change these versions for release or CI builds; update the dependency record
and repeat hardware tests first.

## Windows 10 or 11

### 1. Install the common tools

Open PowerShell as your normal user and install Git, CMake, Ninja, and Python:

```powershell
winget install --id Git.Git --exact
winget install --id Kitware.CMake --exact
winget install --id Ninja-build.Ninja --exact
winget install --id Python.Python.3.12 --exact
```

For host builds, install Visual Studio 2022 Build Tools with its C++ workload:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

The graphical installer from <https://visualstudio.microsoft.com/downloads/>
is an alternative. Select **Desktop development with C++**, including the MSVC
compiler and Windows SDK. Visual Studio Community with the same workload is
also suitable.

Close and reopen PowerShell after installation. Use **Developer PowerShell for
VS 2022** for host builds so `cl.exe` is available.

### 2. Install the RP2040 compiler

Download Arm GNU Toolchain 13.2.Rel1 from
<https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain>. Select the
Windows-hosted **AArch32 bare-metal target (`arm-none-eabi`)** package, not the
similarly named `aarch64-none-elf` package.

Run the installer and enable its option to add the compiler's `bin` directory
to `PATH`. If that option is unavailable, add the installed `bin` directory to
the user `Path` environment variable, then open a new terminal.

### 3. Install the ESP32 toolchains

Install Espressif's ESP-IDF Installation Manager:

```powershell
winget install --id Espressif.EIM --exact
```

Use it to install ESP-IDF v6.0.2 and its tools. The project does not link
ESP-IDF, but this is the supported way to obtain the pinned RISC-V and Xtensa
compilers and esptool together.

For each ESP build session, use EIM's **Open IDF Terminal** action for the
v6.0.2 installation. EIM also displays the exact activation-script command if
you prefer an ordinary PowerShell. Activation applies only to that terminal
and must be repeated in every new one.

### 4. Obtain the source and verify the setup

```powershell
git clone https://github.com/mekatrol/mekatrol-micro-flow.git
Set-Location mekatrol-micro-flow

git --version
cmake --version
ninja --version
python --version
cl
arm-none-eabi-gcc --version
riscv32-esp-elf-gcc --version
xtensa-esp32s3-elf-gcc --version
esptool version
cmake --list-presets=all
```

`cl` may report that no source files were supplied after printing its version;
that is expected. If `esptool` is not exposed as a command, try
`python -m esptool version` and ensure the ESP-IDF environment was activated.

## macOS

### 1. Install the common tools

Install Apple's command-line developer tools, which include Apple Clang:

```sh
xcode-select --install
```

Install Homebrew from <https://brew.sh/> if it is not already present. Then
install Git, CMake, Ninja, and Python:

```sh
brew install git cmake ninja python libgcrypt glib pixman sdl2 libslirp dfu-util
```

Open a new terminal if Homebrew changed your shell configuration.

### 2. Install the RP2040 compiler

Download Arm GNU Toolchain 13.2.Rel1 from
<https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain>. Choose the
macOS package matching the host CPU—Apple silicon (`darwin-arm64`) or Intel
(`darwin-x86_64`)—and the **AArch32 bare-metal `arm-none-eabi` target**.

Install the package. If `arm-none-eabi-gcc` is not found afterward, add the
toolchain's `bin` directory to `PATH` in `~/.zprofile`, then open a new terminal.

### 3. Install the ESP32 toolchains

Install Espressif's ESP-IDF Installation Manager and use it to install ESP-IDF
v6.0.2 with the ESP32-C6 and ESP32-S3 tools. Follow the current macOS command
shown at <https://developer.espressif.com/esp-idf/> because the package name may
differ between Intel and Apple silicon hosts.

Alternatively, perform the official script-based installation:

```sh
mkdir -p "$HOME/esp"
git clone --branch v6.0.2 --recursive --depth 1 \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf"
"$HOME/esp/esp-idf/install.sh" esp32c6,esp32s3
```

Activate it in every terminal used for an ESP build:

```sh
. "$HOME/esp/esp-idf/export.sh"
```

This activation adds both cross-compilers and esptool to the current `PATH`.

### 4. Obtain the source and verify the setup

```sh
git clone https://github.com/mekatrol/mekatrol-micro-flow.git
cd mekatrol-micro-flow

git --version
cmake --version
ninja --version
python3 --version
clang --version
arm-none-eabi-gcc --version
riscv32-esp-elf-gcc --version
xtensa-esp32s3-elf-gcc --version
esptool version
cmake --list-presets=all
```

## Linux

The commands below cover common distributions. Package names on other
distributions may differ. A distribution-provided CMake is suitable only when
it is version 3.25 or newer; otherwise install a current binary from
<https://cmake.org/download/>.

### 1. Install the common tools

Ubuntu or Debian:

```sh
sudo apt update
sudo apt install git cmake ninja-build build-essential python3 python3-venv python3-pip wget flex bison gperf ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

Fedora:

```sh
sudo dnf install git cmake ninja-build gcc gcc-c++ python3 python3-pip wget flex bison gperf ccache libffi-devel openssl-devel dfu-util libusb1-devel
```

Arch Linux:

```sh
sudo pacman -S git cmake ninja gcc python python-pip wget flex bison gperf ccache libffi openssl dfu-util libusb
```

### 2. Install the RP2040 compiler

Download Arm GNU Toolchain 13.2.Rel1 from
<https://developer.arm.com/Tools%20and%20Software/GNU%20Toolchain>. Choose the
Linux package matching the host CPU (`x86_64` or `aarch64`) and the **AArch32
bare-metal `arm-none-eabi` target**.

Extract it to a stable location such as `/opt/arm-gnu-toolchain-13.2.Rel1` or a
directory under your home folder. Add its `bin` directory to `PATH`. For a
per-user installation, add a line like this to `~/.profile`:

```sh
export PATH="$HOME/tools/arm-gnu-toolchain-13.2.Rel1/bin:$PATH"
```

Use the exact directory created by the downloaded archive. Log out and back in,
or source `~/.profile`, before continuing. Distribution packages named
`gcc-arm-none-eabi` may work for local development, but they do not satisfy the
repository's pinned-toolchain requirement unless they provide 13.2.Rel1.

### 3. Install the ESP32 toolchains

Install Espressif's ESP-IDF Installation Manager and select ESP-IDF v6.0.2, or
use the official script-based installation:

```sh
mkdir -p "$HOME/esp"
git clone --branch v6.0.2 --recursive --depth 1 \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf"
"$HOME/esp/esp-idf/install.sh" esp32c6,esp32s3
```

Activate it in every terminal used for an ESP build:

```sh
. "$HOME/esp/esp-idf/export.sh"
```

The installation provides the two Espressif compilers, the managed Python
environment, and esptool. Only the host tools are used by this repository.

### 4. Configure USB access

RP2040 BOOT mode appears as a normal removable drive and usually needs no
special rule. For ESP serial/JTAG access, add your user to the distribution's
serial-port group:

```sh
sudo usermod -a -G dialout "$USER"
```

Arch Linux commonly uses `uucp` instead of `dialout`. Log out and back in after
changing group membership. If the device is still inaccessible, follow
Espressif's current Linux udev instructions for its USB VID/PID; do not run the
build or flashing tools permanently as root.

### 5. Obtain the source and verify the setup

```sh
git clone https://github.com/mekatrol/mekatrol-micro-flow.git
cd mekatrol-micro-flow

git --version
cmake --version
ninja --version
python3 --version
cc --version
arm-none-eabi-gcc --version
riscv32-esp-elf-gcc --version
xtensa-esp32s3-elf-gcc --version
esptool version
cmake --list-presets=all
```

## Build smoke tests

Run the host tests first:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

On Windows PowerShell, the same `cmake` and `ctest` commands work unchanged.

Build the RP2040-Zero image:

```sh
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

After activating the ESP-IDF tool environment, build both ESP images:

```sh
cmake --preset waveshare-esp32-c6-zero-debug
cmake --build --preset waveshare-esp32-c6-zero-debug

cmake --preset waveshare-esp32-s3-zero-debug
cmake --build --preset waveshare-esp32-s3-zero-debug
```

You can instead use `./build.sh <preset>` on Linux/macOS or
`.\build.ps1 <preset>` on Windows. Those scripts delete the entire `build/`
directory before configuring the selected preset. See [BUILD.md](BUILD.md) for
the normal build workflow, output files, flashing commands, and clean-build
behaviour.

## Common setup failures

### CMake is too old

If configuration reports that CMake 3.25 is required, replace the distribution
package with a current official CMake release and open a new terminal.

### Ninja is not found

Confirm `ninja --version` works in the same terminal used to run CMake. On
Debian-derived Linux systems the package is named `ninja-build`, but the
installed command is still `ninja`.

### A cross-compiler is not found

The CMake toolchain files search `PATH`; they do not contain developer-specific
absolute paths. Confirm the exact executable named in the tool summary works in
the current terminal. ESP-IDF environment activation applies only to the
current terminal session and must be repeated after opening a new one.

### esptool is not found

Activate the ESP-IDF environment first. If a standalone Python installation is
preferred, install the pinned version into a virtual environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install esptool==5.1.0
```

On Windows, activate that environment with `.\.venv\Scripts\Activate.ps1`.
Do not commit `.venv`; local virtual environments are developer tooling.

### A stale build uses the wrong compiler

CMake caches compiler paths. Remove only the affected generated directory and
configure it again after correcting `PATH`:

```sh
cmake -E remove_directory build/<preset>
cmake --preset <preset>
```
