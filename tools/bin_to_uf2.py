#!/usr/bin/env python3
"""Wrap an RP2040 flash binary in UF2 blocks."""

import pathlib
import struct
import sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FAMILY_ID_PRESENT = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
FLASH_BASE = 0x10000000
PAYLOAD_SIZE = 256

source = pathlib.Path(sys.argv[1]).read_bytes()
block_count = (len(source) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
blocks = []

for block_number in range(block_count):
    payload = source[block_number * PAYLOAD_SIZE : (block_number + 1) * PAYLOAD_SIZE]
    payload += bytes(PAYLOAD_SIZE - len(payload))
    header = struct.pack(
        "<IIIIIIII",
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        UF2_FAMILY_ID_PRESENT,
        FLASH_BASE + block_number * PAYLOAD_SIZE,
        PAYLOAD_SIZE,
        block_number,
        block_count,
        RP2040_FAMILY_ID,
    )
    blocks.append(header + payload + bytes(476 - len(payload)) + struct.pack("<I", UF2_MAGIC_END))

pathlib.Path(sys.argv[2]).write_bytes(b"".join(blocks))
