#!/usr/bin/env python3
"""
Step 2.13: CH32V003 RS-485 receive hardware tests.

Uses the existing Python protocol encoder so the hardware
tests exercise the actual version-one wire format.

The firmware must remain silent and leave all outputs OFF.

Usage:
    python3 tools/test_rx_hardware.py --port /dev/ttyUSB0

Requires:
    pyserial
"""

import argparse
import time

import serial
from device_tool import REQUEST, Packet, encode_packet

DESTINATION = 0x17
PING = 0x01


def request(sequence: int) -> bytes:
    """Produce one valid PING request with a known sequence."""
    return encode_packet(
        Packet(
            address=DESTINATION,
            sequence=sequence,
            kind=REQUEST,
            command=PING,
        )
    )


def transmit(port: serial.Serial, data: bytes) -> None:
    """
    Write a complete group of bytes and wait for transmission.

    WHY:
    flush() waits for the host serial driver to finish its
    write. It does not prove the MCU has received the frame.
    The MCU debugger counters provide that confirmation.
    """
    port.write(data)
    port.flush()


def run_test(
    port: serial.Serial, name: str, frames: list[bytes], expected_count: int
) -> None:
    print(f"\n{name}")
    print(f"Expected additional valid packets: {expected_count}")

    for frame in frames:
        print("TX:", frame.hex(" "))
        transmit(port, frame)

        # Give the small foreground RX queue time to drain
        # before sending the next independent test frame.
        time.sleep(0.05)

    input("Inspect debugger variables, then press Enter...")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()

    with serial.Serial(
        port=args.port,
        baudrate=19200,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.2,
    ) as port:
        run_test(
            port,
            "TEST 1: Three valid packets",
            [request(0x10), request(0x11), request(0x12)],
            3,
        )

        # Corrupt a byte in the encoded body while preserving
        # the final 0x00 delimiter. The CRC must fail.
        bad_crc = bytearray(request(0x20))
        bad_crc[-2] ^= 0x01

        run_test(
            port,
            "TEST 2: Bad CRC, then valid packet",
            [bytes(bad_crc), request(0x21)],
            1,
        )

        # A COBS code of 0xFF requires 254 following bytes.
        # The short frame below cannot satisfy that length.
        malformed = bytes((0xFF, 0x00))

        run_test(
            port,
            "TEST 3: Invalid COBS, then valid packet",
            [malformed, request(0x30)],
            1,
        )

        # 34 nonzero body bytes exceed the receiver's
        # maximum of 33 encoded body bytes.
        oversized = bytes((0x01,)) * 34 + b"\x00"

        run_test(
            port,
            "TEST 4: Oversized frame, then valid packet",
            [oversized, request(0x40)],
            1,
        )

        print("\nTEST 5: Split frame")
        frame = request(0x50)
        midpoint = len(frame) // 2

        transmit(port, frame[:midpoint])
        print("First half sent.")
        input("Verify counter has NOT increased; press Enter...")

        transmit(port, frame[midpoint:])
        print("Second half sent.")
        input("Verify counter increased by 1; press Enter...")

        print("\nTEST 6: Truncated frame, then recovery")
        frame = request(0x60)

        # End the truncated frame explicitly with a delimiter.
        # This must be rejected by the decoder.
        transmit(port, frame[:4] + b"\x00")
        time.sleep(0.05)

        transmit(port, request(0x61))
        input("Verify exactly one new valid packet; press Enter...")

        print("\nHardware receive tests completed.")


if __name__ == "__main__":
    main()
