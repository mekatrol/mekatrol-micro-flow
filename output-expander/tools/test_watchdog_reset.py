#!/usr/bin/env python3
"""Hardware integration test for the CH32V003 RS-485 watchdog RESET.

Run from the repository root with:
    python3 tools/test_watchdog_reset.py

WARNING: This test energizes ALL EIGHT outputs before resetting the MCU.
Only run with loads that can safely be activated together.
"""

import argparse
import sys
import time
from pathlib import Path

import serial

# Support both `python3 tools/test_watchdog_reset.py` and direct execution.
# device_tool.py lives in the same tools directory as this test utility.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from device_tool import OutputExpander, ProtocolError

D = 0x40
ALL_ON = 0xFF
ALL_OFF = 0x00


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM1")
    parser.add_argument("--address", type=lambda value: int(value, 0), default=0xFF)
    parser.add_argument("--timeout", type=float, default=1.0)
    parser.add_argument("--reset-wait", type=float, default=6.0)
    args = parser.parse_args()

    if not 1 <= args.address <= 0xFE:
        parser.error("--address must be 0x01..0xFE")
    if args.timeout <= 0 or args.reset_wait <= 0:
        parser.error("--timeout and --reset-wait must be positive")

    print("WARNING: all eight physical outputs will be activated.", flush=True)
    reset_acknowledged = False
    try:
        with serial.Serial(args.port, 19200, timeout=0.1) as port:
            device = OutputExpander(port, args.timeout)

            print("1. PING before reset")
            uid_before, major, minor = device.ping(args.address)
            print(f"   UID: {uid_before.hex()}  Firmware: {major}.{minor}")

            print("2. Set all eight outputs ON (0xFF)")
            applied = device.set_outputs(args.address, ALL_ON)
            if applied != ALL_ON:
                raise AssertionError(
                    f"SET_OUTPUTS returned 0x{applied:02X}, expected 0xFF"
                )

            print("3. Verify outputs are ON")
            state = device.outputs(args.address)
            print(f"   Output mask: 0x{state:02X}")
            if state != ALL_ON:
                raise AssertionError(f"Expected ON mask 0xFF, received 0x{state:02X}")

            print("4. Send RESET and verify acknowledgment")
            response = device.request(args.address, RESET_COMMAND)
            if response != b"":
                raise AssertionError(
                    f"Expected empty RESET response, got {response.hex()}"
                )
            reset_acknowledged = True
            print("   RESET acknowledged (zero-byte payload)")

            print(f"5. Wait {args.reset_wait:g} seconds for watchdog expiration")
            time.sleep(args.reset_wait)

            print("6. PING after reset")
            uid_after, major_after, minor_after = device.ping(args.address)
            print(f"   UID: {uid_after.hex()}  Firmware: {major_after}.{minor_after}")
            if (uid_after, major_after, minor_after) != (uid_before, major, minor):
                raise AssertionError("Device identity/version changed after reset")

            print("7. Verify all outputs are OFF")
            state = device.outputs(args.address)
            print(f"   Output mask: 0x{state:02X}")
            if state != ALL_OFF:
                raise AssertionError(f"Expected OFF mask 0x00, received 0x{state:02X}")

        print("PASS: RESET acknowledged; device responsive again; all outputs OFF.")
        print("Note: reset-cause register was not checked; watchdog cause is inferred.")
        return 0

    except (serial.SerialException, ProtocolError, AssertionError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        if not reset_acknowledged:
            print(
                "WARNING: outputs may still be ON. Check the hardware and turn them OFF safely.",
                file=sys.stderr,
            )
        return 1


if __name__ == "__main__":
    sys.exit(main())
