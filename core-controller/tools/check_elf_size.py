#!/usr/bin/env python3
"""Report ELF static memory use and fail when configured limits are exceeded."""

import argparse
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--size-tool", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--flash-limit", required=True, type=int)
    parser.add_argument("--ram-limit", required=True, type=int)
    args = parser.parse_args()

    result = subprocess.run(
        [args.size_tool, args.elf], check=True, capture_output=True, text=True
    )
    lines = [line.split() for line in result.stdout.splitlines() if line.strip()]
    if len(lines) < 2 or len(lines[-1]) < 3:
        print("unable to parse size-tool output", file=sys.stderr)
        return 2
    try:
        text, data, bss = (int(value) for value in lines[-1][:3])
    except ValueError:
        print("unable to parse size-tool values", file=sys.stderr)
        return 2

    flash = text + data
    ram = data + bss
    print(
        f"Micro Flow size: flash={flash}/{args.flash_limit} bytes, "
        f"static-ram={ram}/{args.ram_limit} bytes"
    )
    if flash > args.flash_limit or ram > args.ram_limit:
        print("configured firmware memory limit exceeded", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
