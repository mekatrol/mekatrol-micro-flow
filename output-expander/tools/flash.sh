#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preset=ch32v003-release

if [ "$#" -gt 1 ]; then
    echo "usage: $0 [ch32v003-debug|ch32v003-release]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then preset=$1; fi
case "$preset" in ch32v003-debug|ch32v003-release) ;; *) echo "error: not a flashable preset: $preset" >&2; exit 2;; esac

command -v wlink >/dev/null 2>&1 || { echo "error: wlink is required (pinned version in docs/DEPENDENCIES.md)" >&2; exit 1; }
"$root/tools/build.sh" "$preset"
elf="$root/build/$preset/output-expander.elf"
[ -f "$elf" ] || { echo "error: missing firmware ELF: $elf" >&2; exit 1; }
wlink -v flash --chip CH32V003 --speed low "$elf"
