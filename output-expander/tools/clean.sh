#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
[ "$#" -eq 0 ] || { echo "usage: $0" >&2; exit 2; }
cmake -E remove_directory "$root/build"
