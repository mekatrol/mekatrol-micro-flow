#!/usr/bin/env sh

set -eu

prepare_pico_sdk()
{
    if [ -n "${PICO_SDK_PATH:-}" ] &&
       [ -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake" ]; then
        return
    fi
    for candidate in "$HOME/.pico-sdk/sdk/2.3.1" "$HOME/sdk/pico-sdk-2.3.1"; do
        if [ -f "$candidate/external/pico_sdk_import.cmake" ]; then
            PICO_SDK_PATH=$candidate
            export PICO_SDK_PATH
            printf 'Using Pico SDK: %s\n' "$PICO_SDK_PATH"
            return
        fi
    done
    printf 'Pico SDK 2.3.1 not found; set PICO_SDK_PATH.\n' >&2
    exit 1
}

print_usage()
{
    printf '%s\n' \
        "Usage: ./tools/build.sh <target> [--flash] [--device <name>]" \
        "" \
        "Formats the source tree, deletes build/, configures the selected target," \
        "and builds it." \
        "With --flash, copies its UF2 to the mounted device after building." \
        "The default device name is RPI-RP2." \
        "" \
        "Available targets:" \
        "  host-debug" \
        "  host-release" \
        "  pico-debug" \
        "  waveshare-rp2040-zero-debug" \
        "  pico2-arm-debug" \
        "  pico2-riscv-debug"
}

if [ "$#" -lt 1 ]; then
    print_usage
    exit 2
fi

target=$1
flash=false
device=RPI-RP2
shift

while [ "$#" -gt 0 ]; do
    case "$1" in
        --flash)
            flash=true
            shift
            ;;
        -d|--device)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                printf 'Option %s requires a device name.\n\n' "$1" >&2
                print_usage >&2
                exit 2
            fi
            device=$2
            shift 2
            ;;
        *)
            printf 'Unknown option: %s\n\n' "$1" >&2
            print_usage >&2
            exit 2
            ;;
    esac
done

case "$target" in
    host-debug|host-release)
        required_compiler=
        ;;
    pico-debug|waveshare-rp2040-zero-debug|pico2-arm-debug)
        required_compiler=arm-none-eabi-gcc
        ;;
    pico2-riscv-debug)
        required_compiler=riscv32-unknown-elf-gcc
        ;;
    *)
        printf 'Unknown target: %s\n\n' "$target" >&2
        print_usage >&2
        exit 2
        ;;
esac

if [ "$flash" = true ]; then
    case "$target" in
        pico-debug|waveshare-rp2040-zero-debug)
            ;;
        *)
            printf 'Target is not flashable: %s\n' "$target" >&2
            exit 2
            ;;
    esac
fi

for required_tool in cmake ninja $required_compiler; do
    if ! command -v "$required_tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$required_tool" >&2
        exit 1
    fi
done

clang_format=
for candidate in clang-format clang-format-20 clang-format-19 clang-format-18 clang-format-17 clang-format-16 clang-format-15; do
    if command -v "$candidate" >/dev/null 2>&1; then
        clang_format=$candidate
        break
    fi
done
if [ -z "$clang_format" ]; then
    printf 'Required tool not found: clang-format 15 or newer\n' >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1 &&
   ! command -v python >/dev/null 2>&1; then
    printf 'Required tool not found: Python 3\n' >&2
    exit 1
fi

if [ "$target" = pico-debug ] || [ "$target" = waveshare-rp2040-zero-debug ]; then
    prepare_pico_sdk
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_root"

printf 'Formatting source files...\n'
find src tests -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o \
       -name '*.cpp' -o -name '*.hpp' \) -print |
while IFS= read -r source_file; do
    "$clang_format" -i -- "$source_file"
done
"$script_dir/format-c-functions.sh"

cmake -E remove_directory "$repo_root/build"
cmake --preset "$target"
cmake --build --preset "$target"

if [ "$flash" = true ]; then
    "$script_dir/flash.sh" "$target" --device "$device"
fi
