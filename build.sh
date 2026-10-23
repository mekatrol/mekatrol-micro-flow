#!/usr/bin/env sh

set -eu

PICO_SDK_REQUIRED_VERSION=2.3.1

prepare_pico_sdk()
{
    if [ -n "${PICO_SDK_PATH:-}" ]; then
        if [ -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
            printf 'Using Pico SDK: %s\n' "$PICO_SDK_PATH"
            return 0
        fi
        printf 'PICO_SDK_PATH does not contain pico_sdk_init.cmake: %s\n' \
            "$PICO_SDK_PATH" >&2
        return 1
    fi

    for sdk_path in \
        "$HOME/.pico-sdk/sdk/$PICO_SDK_REQUIRED_VERSION" \
        "$HOME/sdk/pico-sdk-$PICO_SDK_REQUIRED_VERSION"
    do
        if [ -f "$sdk_path/pico_sdk_init.cmake" ]; then
            PICO_SDK_PATH=$sdk_path
            export PICO_SDK_PATH
            printf 'Using discovered Pico SDK: %s\n' "$PICO_SDK_PATH"
            return 0
        fi
    done

    printf 'Required Pico SDK %s was not found.\n' "$PICO_SDK_REQUIRED_VERSION" >&2
    printf 'The VS Code extension is installed, but its SDK payload is not present at:\n' >&2
    printf '  %s\n' "$HOME/.pico-sdk/sdk/$PICO_SDK_REQUIRED_VERSION" >&2
    printf 'Install SDK %s using "Raspberry Pi Pico: New Pico Project", or follow PROJECT_SETUP.md.\n' \
        "$PICO_SDK_REQUIRED_VERSION" >&2
    return 1
}

add_installed_tool_to_path()
{
    tool_name=$1
    prefer_discovered=${2:-no}

    if [ "$prefer_discovered" = no ] && command -v "$tool_name" >/dev/null 2>&1; then
        return 0
    fi

    for search_root in \
        "${IDF_TOOLS_PATH:-}" \
        "${IDF_TOOLS_PATH:+$IDF_TOOLS_PATH/tools}" \
        "$HOME/.espressif/tools" \
        "$HOME/.pico-sdk" \
        "$HOME/.mcuxpressotools" \
        "$HOME/.local/bin" \
        "$HOME/tools" \
        /opt
    do
        if [ ! -d "$search_root" ]; then
            continue
        fi

        tool_path=$(find "$search_root" -type f -name "$tool_name" -perm -u+x -print 2>/dev/null |
            sort -r | sed -n '1p')
        if [ -n "$tool_path" ]; then
            tool_dir=$(dirname -- "$tool_path")
            PATH="$tool_dir:$PATH"
            export PATH
            printf 'Using discovered %s: %s\n' "$tool_name" "$tool_path"
            return 0
        fi
    done

    if command -v "$tool_name" >/dev/null 2>&1; then
        return 0
    fi

    printf 'Required tool not found: %s\n' "$tool_name" >&2
    printf 'Install it or add its directory to PATH, then try again.\n' >&2
    return 1
}

prepare_build_tools()
{
    add_installed_tool_to_path ninja

    case "$1" in
        waveshare-rp2040-zero-debug)
            prepare_pico_sdk
            add_installed_tool_to_path arm-none-eabi-gcc
            ;;
        raspberry-pi-pico-2-arm-debug)
            add_installed_tool_to_path arm-none-eabi-gcc
            ;;
        raspberry-pi-pico-2-riscv-debug)
            add_installed_tool_to_path riscv32-unknown-elf-gcc
            ;;
        waveshare-esp32-c6-zero-debug)
            add_installed_tool_to_path riscv32-esp-elf-gcc
            add_installed_tool_to_path esptool yes
            ;;
        waveshare-esp32-s3-zero-debug)
            add_installed_tool_to_path xtensa-esp32s3-elf-gcc
            add_installed_tool_to_path esptool yes
            ;;
    esac
}

print_usage()
{
    printf '%s\n' \
        "Usage: ./build.sh <target>" \
        "" \
        "Deletes the build directory, configures the selected target, and builds it." \
        "" \
        "Available targets:" \
        "  host-debug    Debug build with tests and supported sanitizers" \
        "  host-release  Optimized release build with tests" \
        "  waveshare-rp2040-zero-debug  RP2040-Zero LED firmware" \
        "  raspberry-pi-pico-2-arm-debug  RP2350 Cortex-M33 compile target" \
        "  raspberry-pi-pico-2-riscv-debug  RP2350 Hazard3 RISC-V compile target" \
        "  waveshare-esp32-c6-zero-debug  ESP32-C6-Zero LED firmware" \
        "  waveshare-esp32-s3-zero-debug  ESP32-S3-Zero LED firmware"
}

if [ "$#" -ne 1 ]; then
    print_usage
    exit 2
fi

case "$1" in
    host-debug|host-release|waveshare-rp2040-zero-debug|raspberry-pi-pico-2-arm-debug|raspberry-pi-pico-2-riscv-debug|waveshare-esp32-c6-zero-debug|waveshare-esp32-s3-zero-debug)
        build_target=$1
        ;;
    *)
        printf 'Unknown target: %s\n\n' "$1" >&2
        print_usage >&2
        exit 2
        ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$script_dir"

prepare_build_tools "$build_target"

cmake -E remove_directory "$script_dir/build"
cmake --preset "$build_target"
cmake --build --preset "$build_target"
