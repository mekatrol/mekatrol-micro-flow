#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preset=host-debug
test_name=

while [ "$#" -gt 0 ]; do
    case "$1" in
        host-debug|host-release) preset=$1 ;;
        --test) shift; [ "$#" -gt 0 ] || { echo "error: --test needs a name" >&2; exit 2; }; test_name=$1 ;;
        -h|--help) echo "usage: $0 [host-debug|host-release] [--test regex]"; exit 0 ;;
        *) echo "error: unknown option or preset: $1" >&2; exit 2 ;;
    esac
    shift
done

"$root/tools/build.sh" "$preset"
if [ -n "$test_name" ]; then
    ctest --test-dir "$root/build/$preset" --output-on-failure -R "$test_name"
else
    ctest --test-dir "$root/build/$preset" --output-on-failure
fi
