#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preset=ch32v003-release
clean=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        host-debug|host-release|ch32v003-debug|ch32v003-release) preset=$1 ;;
        --clean) clean=1 ;;
        -h|--help) echo "usage: $0 [preset] [--clean]"; exit 0 ;;
        *) echo "error: unknown option or preset: $1" >&2; exit 2 ;;
    esac
    shift
done

command -v cmake >/dev/null 2>&1 || { echo "error: cmake is required" >&2; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "error: ninja is required" >&2; exit 1; }
if [ "$clean" -eq 1 ]; then
    cmake -E remove_directory "$root/build/$preset"
fi
cmake --preset "$preset" -S "$root"
(cd "$root" && cmake --build --preset "$preset")
