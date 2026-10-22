#!/usr/bin/env python3
"""Pad an RP2040 second-stage boot binary and append its boot-ROM CRC."""

import binascii
import pathlib
import struct
import sys


def bit_reverse(value, width):
    return int(f"{value:0{width}b}"[::-1], 2)


source = pathlib.Path(sys.argv[1]).read_bytes()
if len(source) > 252:
    raise SystemExit(f"boot2 is {len(source)} bytes; maximum is 252")

padded = source + bytes(252 - len(source))
crc_input = bytes(bit_reverse(value, 8) for value in padded)
checksum = bit_reverse((binascii.crc32(crc_input, 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF, 32)
output = padded + struct.pack("<L", checksum)

assembly = [
    ".syntax unified",
    ".cpu cortex-m0plus",
    ".thumb",
    '.section .boot2, "ax", %progbits',
]
for offset in range(0, len(output), 16):
    assembly.append(".byte " + ", ".join(f"0x{value:02x}" for value in output[offset : offset + 16]))

pathlib.Path(sys.argv[2]).write_text("\n".join(assembly) + "\n", encoding="utf-8")
