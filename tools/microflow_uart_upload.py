#!/usr/bin/env python3
"""Probe or upload an in-RAM flow to the Raspberry Pi Pico supervisor."""

import argparse
import binascii
import struct
import sys
import time

MAGIC = b"MFLO"
VERSION = 1
PROBE, BEGIN, DATA, COMPLETE = 1, 3, 4, 5
DEVICE_INFO, VALIDATION_RESULT, ACTIVATION_RESULT, ERROR = 2, 7, 8, 127
MAX_IMAGE = 2048


def frame(kind: int, sequence: int, payload: bytes = b"") -> bytes:
    fields = struct.pack("<BBHI", VERSION, kind, len(payload), sequence)
    return MAGIC + fields + struct.pack("<I", binascii.crc32(fields + payload)) + payload


def read_exact(port, length: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < length and time.monotonic() < deadline:
        result.extend(port.read(length - len(result)))
    if len(result) != length:
        raise RuntimeError("response timed out")
    return bytes(result)


def receive(port, timeout: float):
    deadline = time.monotonic() + timeout
    window = bytearray()
    while bytes(window[-4:]) != MAGIC:
        window.extend(read_exact(port, 1, deadline))
        if len(window) > 4:
            del window[0]
    header = read_exact(port, 12, deadline)
    version, kind, length, sequence, checksum = struct.unpack("<BBHII", header)
    payload = read_exact(port, length, deadline)
    if version != VERSION or binascii.crc32(header[:8] + payload) != checksum:
        raise RuntimeError("invalid response")
    return kind, sequence, payload


def exchange(port, kind: int, sequence: int, payload: bytes = b"", timeout: float = 2.0):
    port.write(frame(kind, sequence, payload))
    response = receive(port, timeout)
    if response[1] != sequence or response[0] == ERROR:
        code = response[2][0] if response[2] else -1
        raise RuntimeError(f"device rejected sequence {sequence} (error {code})")
    return response


def envelope(payload: bytes) -> bytes:
    header = b"MFFL" + bytes((1, 0)) + struct.pack("<H", 16)
    return header + struct.pack("<II", 16 + len(payload), binascii.crc32(payload)) + payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="serial port, for example /dev/ttyUSB0 or COM5")
    parser.add_argument("image", nargs="?", help="flow payload to wrap and upload")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.error("pyserial is required (python -m pip install pyserial)")
    try:
        with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=2.0) as port:
            kind, _, info = exchange(port, PROBE, 0)
            if kind != DEVICE_INFO:
                raise RuntimeError("unexpected probe response")
            print(f"protocol={info[0]} max_image={int.from_bytes(info[1:3], 'little')}")
            if args.image is None:
                return 0
            with open(args.image, "rb") as source:
                image = envelope(source.read())
            if len(image) > MAX_IMAGE:
                raise RuntimeError(f"wrapped image exceeds {MAX_IMAGE} bytes")
            sequence = 1
            exchange(port, BEGIN, sequence, struct.pack("<II", len(image), binascii.crc32(image)))
            sequence += 1
            for offset in range(0, len(image), 96):
                chunk = image[offset:offset + 96]
                exchange(port, DATA, sequence, struct.pack("<I", offset) + chunk)
                sequence += 1
            port.write(frame(COMPLETE, sequence))
            validation = receive(port, 2.0)
            activation = receive(port, 2.0)
            if validation[0] != VALIDATION_RESULT or validation[2] != b"\0":
                raise RuntimeError("flow validation failed")
            if activation[0] != ACTIVATION_RESULT or activation[2] != b"\0":
                raise RuntimeError("flow activation failed")
            print(f"activated {len(image)} bytes")
    except (OSError, RuntimeError) as error:
        print(f"upload failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
