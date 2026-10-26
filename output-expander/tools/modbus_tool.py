#!/usr/bin/env python3
"""Command-line Modbus RTU utility for the Mekatrol output expander.

Requires pyserial::

    python3 -m pip install pyserial

Examples::

    python3 tools/modbus_tool.py --port /dev/ttyUSB0 info --address 1
    python3 tools/modbus_tool.py --port /dev/ttyUSB0 outputs --address 1
    python3 tools/modbus_tool.py --port /dev/ttyUSB0 outputs --address 1 --set 0x55
    python3 tools/modbus_tool.py --port /dev/ttyUSB0 output --address 1 3 on
    python3 tools/modbus_tool.py --port /dev/ttyUSB0 set-address \
        --uid 00112233445566778899aabb --new-address 7

Output channels in the CLI are numbered 1 through 8. Modbus coil addresses on
the wire remain zero based. Address assignment and readdressing select the
board by its complete 96-bit UID.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass
from typing import Optional, Sequence


BAUDRATE = 19_200
READ_TIMEOUT = 0.25
UID_BYTES = 12


class ModbusError(RuntimeError):
    """A transport error, malformed response, or Modbus exception."""


def crc16(data: bytes) -> int:
    """Return the CRC-16/Modbus checksum for *data*."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def frame(address: int, function: int, payload: bytes = b"") -> bytes:
    message = bytes((address, function)) + payload
    return message + struct.pack("<H", crc16(message))


def parse_uid(value: str) -> bytes:
    compact = value.replace(":", "").replace("-", "").replace(" ", "")
    try:
        uid = bytes.fromhex(compact)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("UID must contain hexadecimal bytes") from exc
    if len(uid) != UID_BYTES:
        raise argparse.ArgumentTypeError("UID must be exactly 12 bytes (24 hex digits)")
    return uid


def slave_address(value: str) -> int:
    try:
        address = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("address must be an integer") from exc
    if not 1 <= address <= 247:
        raise argparse.ArgumentTypeError("address must be between 1 and 247")
    return address


def output_mask(value: str) -> int:
    try:
        mask = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("mask must be an integer") from exc
    if not 0 <= mask <= 0xFF:
        raise argparse.ArgumentTypeError("mask must be between 0x00 and 0xff")
    return mask


@dataclass(frozen=True)
class DeviceInfo:
    address: int
    firmware_major: int
    firmware_minor: int
    output_mask: int
    status: int
    uid: bytes


class OutputExpander:
    """Small Modbus RTU master for the output-expander register map."""

    def __init__(self, serial_port: object, response_timeout: float = READ_TIMEOUT):
        self.serial = serial_port
        self.response_timeout = response_timeout

    def _read_exactly(self, count: int) -> bytes:
        data = bytearray()
        deadline = time.monotonic() + self.response_timeout
        while len(data) < count and time.monotonic() < deadline:
            chunk = self.serial.read(count - len(data))
            if chunk:
                data.extend(chunk)
        if len(data) != count:
            raise ModbusError(f"response timed out ({len(data)}/{count} bytes received)")
        return bytes(data)

    def _request(
        self, address: int, function: int, payload: bytes, response_length: int
    ) -> bytes:
        request = frame(address, function, payload)
        self.serial.reset_input_buffer()
        self.serial.write(request)
        self.serial.flush()

        header = self._read_exactly(2)
        if header[0] != address:
            raise ModbusError(
                f"response address {header[0]} does not match request address {address}"
            )
        if header[1] == function | 0x80:
            tail = self._read_exactly(3)
            response = header + tail
            self._validate_crc(response)
            names = {
                1: "illegal function",
                2: "illegal data address",
                3: "illegal data value",
                4: "slave device failure",
            }
            code = tail[0]
            raise ModbusError(f"device exception {code}: {names.get(code, 'unknown')}")
        if header[1] != function:
            raise ModbusError(
                f"response function 0x{header[1]:02x} does not match 0x{function:02x}"
            )

        response = header + self._read_exactly(response_length - 2)
        self._validate_crc(response)
        return response

    @staticmethod
    def _validate_crc(response: bytes) -> None:
        if len(response) < 4 or crc16(response[:-2]) != int.from_bytes(
            response[-2:], "little"
        ):
            raise ModbusError("response has an invalid CRC")

    def _read_registers(
        self, address: int, function: int, start: int, count: int
    ) -> list[int]:
        response = self._request(
            address, function, struct.pack(">HH", start, count), 5 + count * 2
        )
        if response[2] != count * 2:
            raise ModbusError(f"unexpected register byte count {response[2]}")
        return list(struct.unpack(f">{count}H", response[3:-2]))

    def read_info(self, address: int) -> DeviceInfo:
        configured_address = self._read_registers(address, 0x03, 0, 1)[0]
        values = self._read_registers(address, 0x04, 0, 9)
        if configured_address != address:
            raise ModbusError(
                f"device reports address {configured_address}, expected {address}"
            )
        version = values[0]
        uid = b"".join(word.to_bytes(2, "big") for word in values[3:9])
        return DeviceInfo(
            address=configured_address,
            firmware_major=version >> 8,
            firmware_minor=version & 0xFF,
            output_mask=values[1] & 0xFF,
            status=values[2],
            uid=uid,
        )

    def read_outputs(self, address: int) -> int:
        response = self._request(address, 0x01, struct.pack(">HH", 0, 8), 6)
        if response[2] != 1:
            raise ModbusError(f"unexpected coil byte count {response[2]}")
        return response[3]

    def write_outputs(self, address: int, mask: int) -> None:
        payload = struct.pack(">HHBB", 0, 8, 1, mask)
        response = self._request(address, 0x0F, payload, 8)
        if response[2:-2] != struct.pack(">HH", 0, 8):
            raise ModbusError("device acknowledged a different coil range")

    def write_output(self, address: int, channel: int, on: bool) -> None:
        coil = channel - 1
        payload = struct.pack(">HH", coil, 0xFF00 if on else 0x0000)
        response = self._request(address, 0x05, payload, 8)
        if response[2:-2] != payload:
            raise ModbusError("device acknowledgement does not match the write")

    def assign_address(self, uid: bytes, new_address: int) -> None:
        # Function 0x41 is a broadcast and therefore intentionally has no reply.
        request = frame(0, 0x41, bytes((0x03,)) + uid + bytes((new_address,)))
        self.serial.reset_input_buffer()
        self.serial.write(request)
        self.serial.flush()
        time.sleep(0.05)

        # Assignment is not considered successful until the new address proves
        # that the same physical device owns it.
        actual = self.read_info(new_address).uid
        if actual != uid:
            raise ModbusError(
                f"UID verification failed: expected {uid.hex()}, got {actual.hex()}"
            )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial device, e.g. /dev/ttyUSB0")
    parser.add_argument("--timeout", type=float, default=READ_TIMEOUT, help="serial timeout in seconds")
    subparsers = parser.add_subparsers(dest="command", required=True)

    info = subparsers.add_parser("info", help="read address, firmware, UID, status, and outputs")
    info.add_argument("--address", required=True, type=slave_address)

    outputs = subparsers.add_parser("outputs", help="read or set all eight outputs")
    outputs.add_argument("--address", required=True, type=slave_address)
    outputs.add_argument("--set", dest="mask", type=output_mask, help="output bitmask, e.g. 0x55")

    output = subparsers.add_parser("output", help="turn one output on or off")
    output.add_argument("--address", required=True, type=slave_address)
    output.add_argument("channel", type=int, choices=range(1, 9), metavar="CHANNEL")
    output.add_argument("state", choices=("on", "off"))

    assign = subparsers.add_parser("set-address", help="assign or change a device address by UID")
    assign.add_argument("--uid", required=True, type=parse_uid)
    assign.add_argument("--new-address", required=True, type=slave_address)
    return parser


def print_outputs(mask: int) -> None:
    states = " ".join(
        f"{channel}={'ON' if mask & (1 << (channel - 1)) else 'off'}"
        for channel in range(1, 9)
    )
    print(f"outputs: 0x{mask:02x} ({states})")


def run(args: argparse.Namespace) -> None:
    try:
        import serial
    except ImportError as exc:
        raise ModbusError(
            "pyserial is required; install it with: python3 -m pip install pyserial"
        ) from exc

    with serial.Serial(
        port=args.port,
        baudrate=BAUDRATE,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_EVEN,
        stopbits=serial.STOPBITS_ONE,
        timeout=args.timeout,
        write_timeout=args.timeout,
    ) as port:
        device = OutputExpander(port, args.timeout)
        if args.command == "info":
            info = device.read_info(args.address)
            print(f"address: {info.address}")
            print(f"firmware: {info.firmware_major}.{info.firmware_minor}")
            print(f"uid: {info.uid.hex()}")
            print(f"status: 0x{info.status:04x}")
            print_outputs(info.output_mask)
        elif args.command == "outputs":
            if args.mask is not None:
                device.write_outputs(args.address, args.mask)
            print_outputs(device.read_outputs(args.address))
        elif args.command == "output":
            device.write_output(args.address, args.channel, args.state == "on")
            print_outputs(device.read_outputs(args.address))
        elif args.command == "set-address":
            device.assign_address(args.uid, args.new_address)
            print(f"assigned UID {args.uid.hex()} to address {args.new_address}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    try:
        run(build_parser().parse_args(argv))
    except (ModbusError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
