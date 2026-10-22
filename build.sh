#!/usr/bin/env sh

set -eu

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
        "  waveshare-esp32-c6-zero-debug  ESP32-C6-Zero LED firmware" \
        "  waveshare-esp32-s3-zero-debug  ESP32-S3-Zero LED firmware"
}

if [ "$#" -ne 1 ]; then
    print_usage
    exit 2
fi

case "$1" in
    host-debug|host-release|waveshare-rp2040-zero-debug|waveshare-esp32-c6-zero-debug|waveshare-esp32-s3-zero-debug)
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

cmake -E remove_directory "$script_dir/build"
cmake --preset "$build_target"
cmake --build --preset "$build_target"
