#!/usr/bin/env sh

set -eu

print_usage()
{
    printf '%s\n' \
        "Usage: ./tools/flash.sh <target> [--device <name>]" \
        "" \
        "Builds the selected target if needed, then copies its UF2 to the mounted device." \
        "The default device name is RPI-RP2." \
        "" \
        "Flashable targets:" \
        "  pico-debug" \
        "  waveshare-rp2040-zero-debug"
}

if [ "$#" -lt 1 ]; then
    print_usage
    exit 2
fi

target=$1
device=RPI-RP2
shift

while [ "$#" -gt 0 ]; do
    case "$1" in
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
    pico-debug|waveshare-rp2040-zero-debug)
        firmware_name=microflow-raspberry-pi-pico.uf2
        ;;
    *)
        printf 'Target is not flashable: %s\n\n' "$target" >&2
        print_usage >&2
        exit 2
        ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
firmware="$repo_root/build/$target/$firmware_name"

for required_tool in cmake ninja arm-none-eabi-gcc; do
    if ! command -v "$required_tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$required_tool" >&2
        exit 1
    fi
done

if [ -z "${PICO_SDK_PATH:-}" ] ||
   [ ! -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake" ]; then
    for candidate in "$HOME/.pico-sdk/sdk/2.3.1" "$HOME/sdk/pico-sdk-2.3.1"; do
        if [ -f "$candidate/external/pico_sdk_import.cmake" ]; then
            PICO_SDK_PATH=$candidate
            export PICO_SDK_PATH
            printf 'Using Pico SDK: %s\n' "$PICO_SDK_PATH"
            break
        fi
    done
fi
if [ -z "${PICO_SDK_PATH:-}" ] ||
   [ ! -f "$PICO_SDK_PATH/external/pico_sdk_import.cmake" ]; then
    printf 'Pico SDK 2.3.1 not found; set PICO_SDK_PATH.\n' >&2
    exit 1
fi

cd "$repo_root"
if [ ! -f "$repo_root/build/$target/build.ninja" ]; then
    cmake --preset "$target"
fi
cmake --build --preset "$target"

user_name=${USER:-$(id -un)}
mount_point="/media/$user_name/$device"
if [ ! -d "$mount_point" ]; then
    printf 'Device is not mounted at: %s\n' "$mount_point" >&2
    exit 1
fi

printf 'Flashing %s to %s\n' "$target" "$mount_point"
cp "$firmware" "$mount_point/"
printf 'Flash complete.\n'
