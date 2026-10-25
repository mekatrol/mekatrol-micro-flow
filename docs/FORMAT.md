# Source Formatting

Run the formatters from the repository root. The project uses two formatting
tools:

1. clang-format applies the rules in [`.clang-format`](../.clang-format) to C
   and C++ source and header files. This includes adding braces to single-statement
   control-flow bodies.
2. `tools/format_c_functions.py` ensures there is exactly one empty line between
   adjacent top-level C function definitions. The Bash and PowerShell wrappers
   run it recursively over `src`.

clang-format 15 or newer is required because the configuration uses
`InsertBraces`.

The clean-build wrappers run both formatters automatically before configuring
or compiling a target:

```sh
./tools/build.sh host-debug
```

```powershell
.\tools\build.ps1 host-debug
```

Consequently, running a build wrapper can modify tracked source files. The
wrappers use `clang-format` when available and also recognize versioned command
names from `clang-format-15` through `clang-format-20`. The commands below are
useful when formatting separately from a build.

## Install clang-format

### Linux

On Debian, Ubuntu, and Linux Mint:

```sh
sudo apt install clang-format
```

Confirm that the command is available:

```sh
clang-format --version
```

### macOS

Install clang-format with Homebrew:

```sh
brew install clang-format
```

Confirm that the command is available:

```sh
clang-format --version
```

### Windows

Install LLVM with Windows Package Manager from PowerShell:

```powershell
winget install --id LLVM.LLVM --exact
```

Open a new PowerShell session, then confirm that the command is available:

```powershell
clang-format --version
```

If the command is not found, add the LLVM `bin` directory, normally
`C:\Program Files\LLVM\bin`, to `PATH`.

## Format all source files

Run clang-format first, followed by the project-specific function-spacing
formatter.

### Linux and macOS

```sh
find src tests -type f \
  \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o \
     -name '*.cpp' -o -name '*.hpp' \) -print0 |
  xargs -0 -r clang-format -i
./tools/format-c-functions.sh
```

The `-r` option to `xargs` is supported by GNU `xargs` on Linux. On macOS, use
this portable form instead:

```sh
find src tests -type f \
  \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o \
     -name '*.cpp' -o -name '*.hpp' \) -print0 |
  xargs -0 clang-format -i
./tools/format-c-functions.sh
```

### Windows

Run these commands from PowerShell:

```powershell
Get-ChildItem -Path src, tests -Recurse -File |
  Where-Object { $_.Extension -in '.c', '.h', '.cc', '.cpp', '.hpp' } |
  ForEach-Object { clang-format -i -- $_.FullName }
.\tools\format-c-functions.ps1
```

## Check function spacing without changing files

The project-specific formatter supports a check-only mode. It exits with a
non-zero status and lists files when changes are required.

On Linux and macOS:

```sh
./tools/format-c-functions.sh --check
```

On Windows PowerShell:

```powershell
.\tools\format-c-functions.ps1 --check
```

The wrappers invoke `tools/format_c_functions.py` with the `src` directory. To
run it directly against particular files or directories, use:

```sh
python3 tools/format_c_functions.py path/to/file.c path/to/directory
```

After formatting, review the changes before committing:

```sh
git diff --check
git diff --stat
git diff
```
