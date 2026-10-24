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
        "Usage: ./build.sh <target>" \
        "" \
        "Deletes build/, configures the selected target, and builds it." \
        "" \
        "Available targets:" \
        "  host-debug" \
        "  host-release" \
        "  pico-debug" \
        "  pico2-arm-debug" \
        "  pico2-riscv-debug"
}

if [ "$#" -ne 1 ]; then
    print_usage
    exit 2
fi

case "$1" in
    host-debug|host-release)
        required_compiler=
        ;;
    pico-debug|pico2-arm-debug)
        required_compiler=arm-none-eabi-gcc
        ;;
    pico2-riscv-debug)
        required_compiler=riscv32-unknown-elf-gcc
        ;;
    *)
        printf 'Unknown target: %s\n\n' "$1" >&2
        print_usage >&2
        exit 2
        ;;
esac

for required_tool in cmake ninja $required_compiler; do
    if ! command -v "$required_tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$required_tool" >&2
        exit 1
    fi
done

if [ "$1" = pico-debug ]; then
    prepare_pico_sdk
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

cmake -E remove_directory "$script_dir/build"
cmake --preset "$1"
cmake --build --preset "$1"
