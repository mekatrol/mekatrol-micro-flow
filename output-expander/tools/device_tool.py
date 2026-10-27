#!/usr/bin/env python3
"""Reference CLI for the output-expander proprietary RS-485 protocol.

The framing functions intentionally mirror the no-heap firmware codec. Keeping
an independent host implementation makes it possible to verify byte order,
COBS framing, CRC behavior, and response matching without special libraries.
"""

import argparse
import sys
import time
from collections.abc import Sequence
from dataclasses import dataclass

# These values are part of protocol version one rather than user preferences.
BAUDRATE = 19_200
READ_TIMEOUT = 0.1
VERSION = 1
REQUEST, RESPONSE, ERROR = 0, 1, 2
MAX_PAYLOAD = 24

# Translate numeric device errors at the CLI boundary so the wire codec remains
# concerned only with bytes and callers receive an actionable diagnostic.
ERROR_NAMES = {
    1: "unknown command",
    2: "bad length",
    3: "bad value",
    4: "storage error",
    5: "busy",
    6: "unsupported",
}


class ProtocolError(RuntimeError):
    """Report transport failures and protocol-invalid device responses."""


def crc16(data: bytes) -> int:
    """Calculate CRC-16/CCITT-FALSE over decoded packet bytes."""

    # CCITT-FALSE starts with all bits set and processes each byte from its most
    # significant bit using polynomial 0x1021, matching the embedded codec.
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    """Remove zero bytes using Consistent Overhead Byte Stuffing."""

    # Reserve the first byte for the length of the initial non-zero run. Each
    # source zero finishes a run and is represented implicitly by the next code.
    output = bytearray((0,))
    code_index = 0
    code = 1
    for byte in data:
        if byte == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(byte)
            code += 1

            # COBS can describe no more than 254 data bytes with one code. The
            # branch keeps this helper general even though v1 packets are smaller.
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1

    output[code_index] = code
    return bytes(output)


def cobs_decode(data: bytes) -> bytes:
    """Restore a COBS body, rejecting codes which exceed its input."""

    output = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        if code == 0 or index + code > len(data) + 1:
            raise ProtocolError("invalid COBS frame")

        # Copy the run following this code, then restore the suppressed zero
        # between runs. There is no restored zero after the final run.
        index += 1
        output.extend(data[index : index + code - 1])
        index += code - 1
        if code != 0xFF and index < len(data):
            output.append(0)
    return bytes(output)


@dataclass(frozen=True)
class Packet:
    """One decoded protocol packet independent of serial transport."""

    address: int
    sequence: int
    kind: int
    command: int
    payload: bytes = b""


def encode_packet(packet: Packet) -> bytes:
    """Build, checksum, COBS-encode, and delimit one packet."""

    # Reject oversize data rather than truncating it into a different command.
    if len(packet.payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 24 bytes")

    # Construct fields explicitly so host structure layout cannot affect the
    # protocol. CRC is little-endian and covers header through final payload.
    decoded = bytes(
        (
            VERSION,
            packet.address,
            packet.sequence,
            packet.kind,
            packet.command,
            len(packet.payload),
        )
    ) + packet.payload
    protected = decoded + crc16(decoded).to_bytes(2, "little")

    # COBS guarantees the body contains no zero, making this delimiter unique.
    return cobs_encode(protected) + b"\0"


def decode_packet(wire: bytes) -> Packet:
    """Decode and fully validate one optional-delimiter wire frame."""

    decoded = cobs_decode(wire.removesuffix(b"\0"))

    # Validate the minimum header, version, and packet type before indexing any
    # payload fields which depend on those semantics.
    if (
        len(decoded) < 8
        or decoded[0] != VERSION
        or decoded[3] not in (REQUEST, RESPONSE, ERROR)
    ):
        raise ProtocolError("invalid packet header")

    # LEN must describe the packet exactly; trailing or missing bytes indicate
    # corruption even when the physical serial read reached a delimiter.
    if decoded[5] > MAX_PAYLOAD or len(decoded) != 8 + decoded[5]:
        raise ProtocolError("invalid packet length")

    expected_crc = int.from_bytes(decoded[-2:], "little")
    if crc16(decoded[:-2]) != expected_crc:
        raise ProtocolError("invalid packet CRC")

    return Packet(decoded[1], decoded[2], decoded[3], decoded[4], decoded[6:-2])


def parse_uid(value: str) -> bytes:
    """Accept a 96-bit UID with optional colon or hyphen separators."""

    try:
        uid = bytes.fromhex(value.replace(":", "").replace("-", ""))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("UID must be hexadecimal") from exc
    if len(uid) != 12:
        raise argparse.ArgumentTypeError("UID must be exactly 12 bytes")
    return uid


def address(value: str) -> int:
    """Parse a legal version-one unicast address."""

    try:
        result = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("address must be an integer") from exc
    if not 1 <= result <= 0xFE:
        raise argparse.ArgumentTypeError("address must be between 1 and 254")
    return result


def mask(value: str) -> int:
    """Parse the complete eight-channel logical output mask."""

    try:
        result = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("mask must be an integer") from exc
    if not 0 <= result <= 0xFF:
        raise argparse.ArgumentTypeError("mask must be between 0 and 255")
    return result


class OutputExpander:
    """Single-outstanding-request master for one shared RS-485 bus."""

    def __init__(self, port: object, timeout: float = READ_TIMEOUT):
        self.port = port
        self.timeout = timeout
        self.sequence = 0

    def _read_frame(self) -> bytes:
        """Read through one zero delimiter within the response deadline."""

        data = bytearray()
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            byte = self.port.read(1)
            if byte == b"\0":
                return bytes(data)
            if byte:
                data.extend(byte)
        raise ProtocolError("response timed out")

    def request(self, addr: int, command: int, payload: bytes = b"") -> bytes:
        """Send one unicast request and validate its correlated response."""

        # Increment modulo 256 only after preserving this transaction's value.
        # Clearing stale input prevents a late prior response being misread.
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFF
        self.port.reset_input_buffer()
        self.port.write(encode_packet(Packet(addr, sequence, REQUEST, command, payload)))
        self.port.flush()

        reply = decode_packet(self._read_frame())

        # Address, sequence, and command together bind the reply to the only
        # outstanding request and reject delayed or cross-device traffic.
        if (reply.address, reply.sequence, reply.command) != (
            addr,
            sequence,
            command,
        ):
            raise ProtocolError("response does not match request")

        if reply.kind == ERROR:
            code = reply.payload[0] if len(reply.payload) == 1 else 0
            raise ProtocolError(
                f"device error {code}: {ERROR_NAMES.get(code, 'unknown')}"
            )
        if reply.kind != RESPONSE:
            raise ProtocolError("unexpected packet type")
        return reply.payload

    def ping(self, addr: int) -> tuple[bytes, int, int]:
        """Return full silicon UID and firmware major/minor version."""

        payload = self.request(addr, 0x01)
        if len(payload) != 14:
            raise ProtocolError("invalid PING response")
        return payload[:12], payload[12], payload[13]

    def status(self, addr: int) -> tuple[int, int, int]:
        """Return output mask, input mask, and little-endian status bits."""

        payload = self.request(addr, 0x02)
        if len(payload) != 4:
            raise ProtocolError("invalid GET_STATUS response")
        return payload[0], payload[1], int.from_bytes(payload[2:], "little")

    def outputs(self, addr: int) -> int:
        """Read the current logical state of all eight outputs."""

        payload = self.request(addr, 0x11)
        if len(payload) != 1:
            raise ProtocolError("invalid GET_OUTPUTS response")
        return payload[0]

    def set_outputs(self, addr: int, value: int) -> int:
        """Replace all output bits and return the device-applied mask."""

        payload = self.request(addr, 0x10, bytes((value,)))
        if len(payload) != 1:
            raise ProtocolError("invalid SET_OUTPUTS response")
        return payload[0]

    def config(self, addr: int) -> bytes:
        """Read the compact configuration payload without reinterpreting it."""

        return self.request(addr, 0x20)

    def assign(self, uid: bytes, new_address: int) -> None:
        """Broadcast UID-targeted assignment, then verify it by PING."""

        # Broadcasts intentionally have no reply. Advance the sequence after TX
        # and prove success by matching the complete UID at the new address.
        packet = Packet(0, self.sequence, REQUEST, 0x30, uid + bytes((new_address,)))
        self.port.reset_input_buffer()
        self.port.write(encode_packet(packet))
        self.port.flush()
        self.sequence = (self.sequence + 1) & 0xFF
        time.sleep(0.05)
        if self.ping(new_address)[0] != uid:
            raise ProtocolError("UID verification failed")

    def reset(self, addr: int) -> None:
        """Request reset, waiting for acknowledgement before returning."""

        self.request(addr, 0x40)

    def send_bad_crc(self, addr: int) -> bytes:
        """Send one intentionally corrupted packet for fault-path testing."""

        wire = bytearray(encode_packet(Packet(addr, self.sequence, REQUEST, 0x01)))

        # Flip an encoded body byte rather than the delimiter. The device must
        # reject the resulting CRC/COBS failure without transmitting a reply.
        wire[-2] ^= 1
        self.port.reset_input_buffer()
        self.port.write(wire)
        self.port.flush()
        self.sequence = (self.sequence + 1) & 0xFF
        return bytes(wire)


def build_parser() -> argparse.ArgumentParser:
    """Describe CLI commands while sharing protocol-specific validators."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--timeout", type=float, default=READ_TIMEOUT)
    commands = parser.add_subparsers(dest="command", required=True)

    for name in ("info", "status", "outputs", "config", "reset", "simulate-error"):
        command = commands.add_parser(name)
        command.add_argument("--address", required=True, type=address)
        if name == "outputs":
            command.add_argument("--set", type=mask)

    one = commands.add_parser("output")
    one.add_argument("--address", required=True, type=address)
    one.add_argument("channel", type=int, choices=range(1, 9))
    one.add_argument("state", choices=("on", "off"))

    assign = commands.add_parser("set-address")
    assign.add_argument("--uid", required=True, type=parse_uid)
    assign.add_argument("--new-address", required=True, type=address)
    return parser


def show_outputs(value: int) -> None:
    """Print both hexadecimal and per-channel representations."""

    states = " ".join(
        f"{channel}={'ON' if value & 1 << (channel - 1) else 'OFF'}"
        for channel in range(1, 9)
    )
    print(f"outputs: 0x{value:02x} {states}")


def run(args: argparse.Namespace) -> None:
    """Open the requested adapter and execute exactly one CLI operation."""

    try:
        import serial
    except ImportError as exc:
        raise ProtocolError("pyserial is required") from exc

    # Match firmware's fixed 19,200 8N1 physical layer. Context management
    # closes the adapter even when protocol validation raises an exception.
    with serial.Serial(
        args.port,
        BAUDRATE,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=args.timeout,
        write_timeout=args.timeout,
    ) as port:
        device = OutputExpander(port, args.timeout)
        if args.command == "info":
            uid, major, minor = device.ping(args.address)
            outputs, inputs, status = device.status(args.address)
            print(f"address: {args.address}")
            print(f"firmware: {major}.{minor}")
            print(f"uid: {uid.hex()}")
            print(f"inputs: 0x{inputs:02x}")
            print(f"status: 0x{status:04x}")
            show_outputs(outputs)
        elif args.command == "status":
            outputs, inputs, status = device.status(args.address)
            print(f"outputs=0x{outputs:02x} inputs=0x{inputs:02x} status=0x{status:04x}")
        elif args.command == "outputs":
            value = (
                device.set_outputs(args.address, args.set)
                if args.set is not None
                else device.outputs(args.address)
            )
            show_outputs(value)
        elif args.command == "output":
            # Read-modify-write preserves every channel except the requested one.
            value = device.outputs(args.address)
            bit = 1 << (args.channel - 1)
            value = value | bit if args.state == "on" else value & ~bit
            show_outputs(device.set_outputs(args.address, value))
        elif args.command == "config":
            print(device.config(args.address).hex(" "))
        elif args.command == "set-address":
            device.assign(args.uid, args.new_address)
            print(f"assigned {args.uid.hex()} to {args.new_address}")
        elif args.command == "reset":
            device.reset(args.address)
            print(f"board {args.address} acknowledged reset")
        elif args.command == "simulate-error":
            print(
                "sent bad-CRC request: "
                f"{device.send_bad_crc(args.address).hex(' ')}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    """Convert expected user/transport failures into a nonzero CLI result."""

    try:
        run(build_parser().parse_args(argv))
    except (ProtocolError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
