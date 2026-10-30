#!/usr/bin/env python3
"""CLI for the commands implemented by the current output-expander firmware.

The framing functions intentionally mirror the no-heap firmware codec.
An independent host implementation verifies byte order, COBS encoding,
CRC validation, and response matching without special libraries.

The current firmware implements bus-wide UID/address discovery, output control,
persistent configuration, factory reinitialisation, RESET, and UID-targeted
address assignment.
"""

import argparse
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass

# These values are part of protocol version one rather than user preferences.
BAUDRATE = 19_200
READ_TIMEOUT = 0.1
VERSION = 1
REQUEST, RESPONSE, ERROR = 0, 1, 2
MAX_PAYLOAD = 24

# Translate numeric device errors at the CLI boundary so the protocol codec
# remains concerned only with bytes and callers receive an actionable diagnostic.
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
    decoded = (
        bytes(
            (
                VERSION,
                packet.address,
                packet.sequence,
                packet.kind,
                packet.command,
                len(packet.payload),
            )
        )
        + packet.payload
    )
    protected = decoded + crc16(decoded).to_bytes(2, "little")

    # COBS guarantees the body contains no zero, making this delimiter unique.
    return cobs_encode(protected) + b"\0"


def decode_packet(encoded_frame: bytes) -> Packet:
    """Decode and validate a COBS frame with an optional zero delimiter."""

    decoded = cobs_decode(encoded_frame.removesuffix(b"\0"))

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


def ping_address(value: str) -> int:
    """Parse a commissioned address or the isolated discovery address 0xFF."""

    try:
        result = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("address must be an integer") from exc
    if not 1 <= result <= 0xFF:
        raise argparse.ArgumentTypeError("address must be between 1 and 255")
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
        self.port.write(
            encode_packet(Packet(addr, sequence, REQUEST, command, payload))
        )
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

    def outputs(self, addr: int) -> int:
        """Read the current logical state of all eight outputs."""

        payload = self.request(addr, 0x11)
        if len(payload) != 1:
            raise ProtocolError("invalid GET_OUTPUTS response")
        return payload[0]

    def set_outputs(self, addr: int, value: int) -> int:
        """Replace the target mask and return the accepted target."""

        payload = self.request(addr, 0x10, bytes((value,)))
        if len(payload) != 1:
            raise ProtocolError("invalid SET_OUTPUTS response")
        return payload[0]

    def output_delay(self, addr: int) -> int:
        """Read the configured output activation spacing in milliseconds."""
        payload = self.request(addr, 0x12)
        if len(payload) != 1:
            raise ProtocolError("invalid GET_OUTPUT_DELAY response")
        return payload[0] * 100

    def set_output_delay(self, addr: int, delay_ms: int) -> int:
        """Set and return the output activation spacing."""
        if delay_ms % 100 != 0:
            raise ValueError("output delay must be a multiple of 100 ms")
        payload = self.request(addr, 0x13, bytes((delay_ms // 100,)))
        if len(payload) != 1:
            raise ProtocolError("invalid SET_OUTPUT_DELAY response")
        return payload[0] * 100

    def config(self, addr: int) -> tuple[int, int, int, int, int]:
        """Read address, startup mask, timeout, timeout mask, and brightness."""
        payload = self.request(addr, 0x20)
        if len(payload) != 6:
            raise ProtocolError("invalid GET_CONFIG response")
        return payload[0], payload[1], int.from_bytes(payload[2:4], "little"), payload[4], payload[5]

    def set_config(self, addr: int, startup: int, timeout_ms: int,
                   timeout_mask: int, brightness: int) -> None:
        """Commit the complete configurable field group transactionally."""
        payload = bytes((startup,)) + timeout_ms.to_bytes(2, "little") + bytes((timeout_mask, brightness))
        if self.request(addr, 0x21, payload) != payload:
            raise ProtocolError("invalid SET_CONFIG response")

    def factory_reset(self, addr: int) -> None:
        """Restore defaults, including the uncommissioned address 0xff."""
        if self.request(addr, 0x22):
            raise ProtocolError("invalid FACTORY_RESET response")

    def assign(self, uid: bytes, new_address: int) -> None:
        """Assign a persistent address, then verify it by PING."""

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

    def discover(
        self,
        duration: float,
        on_discovered: Callable[[bytes, int], None] | None = None,
    ) -> list[tuple[bytes, int]]:
        """Return UID/address pairs and optionally report each as it arrives."""

        if not 0.1 <= duration <= 15.0:
            raise ValueError("discovery duration must be between 0.1 and 15 seconds")

        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFF
        self.port.reset_input_buffer()
        self.port.write(encode_packet(Packet(0, sequence, REQUEST, 0x31)))
        self.port.flush()

        # Keep the first response for each physical UID. Duplicate configured
        # addresses are intentionally retained because finding that fault is a
        # primary reason for address-independent discovery.
        found: dict[bytes, int] = {}
        deadline = time.monotonic() + duration
        try:
            while time.monotonic() < deadline:
                try:
                    reply = decode_packet(self._read_frame())
                except ProtocolError:
                    # A timeout means no complete response arrived in this
                    # short interval. A malformed frame may be the residue of
                    # two devices choosing the same initial slot. In both cases
                    # keep listening: later responders must still be collected,
                    # and the firmware's own hard deadline bounds the scan.
                    continue

                if (
                    reply.kind != RESPONSE
                    or reply.command != 0x31
                    or reply.sequence != sequence
                    or len(reply.payload) != 13
                ):
                    continue

                uid, current_address = reply.payload[:12], reply.payload[12]
                if reply.address != current_address:
                    raise ProtocolError("discovery response address mismatch")
                # Notify only when this UID is first discovered or its reported
                # address changes. A repeated response must not produce noisy
                # duplicate lines, but the callback runs here—inside the read
                # loop—so an operator does not wait for the scan duration to
                # expire before seeing a device that has already answered.
                if found.get(uid) != current_address:
                    found[uid] = current_address
                    if on_discovered is not None:
                        on_discovered(uid, current_address)
        finally:
            # Always cancel at the end of the listening window, including when
            # decoding fails. This promptly disarms devices that are still
            # deferring instead of relying on their 15-second safety deadline.
            self.port.write(encode_packet(Packet(0, sequence, REQUEST, 0x32)))
            self.port.flush()

        return sorted(found.items(), key=lambda item: (item[1], item[0]))

    def reset(self, addr: int) -> None:
        """Request reset, waiting for acknowledgement before returning."""

        self.request(addr, 0x40)

    def send_bad_crc(self, addr: int) -> bytes:
        """Send one intentionally corrupted packet for fault-path testing."""

        encoded_frame = bytearray(
            encode_packet(Packet(addr, self.sequence, REQUEST, 0x01))
        )

        # Flip an encoded body byte rather than the delimiter. The device must
        # reject the resulting CRC/COBS failure without transmitting a reply.
        encoded_frame[-2] ^= 1
        self.port.reset_input_buffer()
        self.port.write(encoded_frame)
        self.port.flush()
        self.sequence = (self.sequence + 1) & 0xFF
        return bytes(encoded_frame)


def build_parser() -> argparse.ArgumentParser:
    """Describe CLI commands while sharing protocol-specific validators."""

    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--port", required=True, help="serial adapter device")
    parser.add_argument(
        "--timeout",
        type=float,
        default=READ_TIMEOUT,
        help=f"response timeout in seconds (default: {READ_TIMEOUT})",
    )
    commands = parser.add_subparsers(dest="command", required=True)

    command_help = {
        "scan": "discover every connected device UID and current address",
        "info": "show UID, firmware version, and outputs",
        "outputs": "read or replace the complete output mask",
        "output-delay": "read or set the output activation delay",
        "config": "read or replace persistent configuration fields",
        "factory-reset": "restore defaults and address 0xff",
        "reset": "request an acknowledged watchdog reset",
        "simulate-error": "send a deliberately invalid-CRC PING",
    }
    for name, help_text in command_help.items():
        command = commands.add_parser(name, help=help_text)
        if name == "scan":
            command.add_argument(
                "--duration",
                type=float,
                default=1.0,
                help="listening window in seconds, 0.1 through 15 (default: 1)",
            )
            continue
        command.add_argument(
            "--address",
            required=True,
            type=ping_address if name == "info" else address,
        )
        if name == "outputs":
            command.add_argument("--set", type=mask)
        if name == "output-delay":
            command.add_argument(
                "--set", type=int, choices=range(500, 10001, 100)
            )
        if name == "config":
            command.add_argument("--startup-mask", type=mask)
            command.add_argument("--timeout-ms", type=int, choices=range(0, 65536))
            command.add_argument("--timeout-mask", type=mask)
            command.add_argument("--brightness", type=int, choices=range(0, 256))

    one = commands.add_parser("output", help="change one output channel")
    one.add_argument("--address", required=True, type=address)
    one.add_argument("channel", type=int, choices=range(1, 9))
    one.add_argument("state", choices=("on", "off"))

    assign = commands.add_parser(
        "set-address",
        help="assign and verify a persistent address",
        description=(
            "Assign an address to the matching UID and commit it to verified "
            "persistent configuration storage."
        ),
    )
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
        if args.command == "scan":
            def show_discovered(uid: bytes, current_address: int) -> None:
                # Explicit flushing also gives immediate output when this tool
                # is launched through a service, pipe, or logging supervisor
                # whose stdout is block-buffered rather than attached to a TTY.
                print(
                    f"address: 0x{current_address:02x} uid: {uid.hex()}",
                    flush=True,
                )

            discovered = device.discover(args.duration, show_discovered)
            print(f"devices: {len(discovered)}")
        elif args.command == "info":
            uid, major, minor = device.ping(args.address)
            print(f"address: {args.address}")
            print(f"firmware: {major}.{minor}")
            print(f"uid: {uid.hex()}")
            if args.address != 0xFF:
                show_outputs(device.outputs(args.address))
        elif args.command == "outputs":
            value = (
                device.set_outputs(args.address, args.set)
                if args.set is not None
                else device.outputs(args.address)
            )
            show_outputs(value)
        elif args.command == "output-delay":
            value = (
                device.set_output_delay(args.address, args.set)
                if args.set is not None
                else device.output_delay(args.address)
            )
            print(f"output activation delay: {value} ms")
        elif args.command == "config":
            supplied = (args.startup_mask, args.timeout_ms, args.timeout_mask, args.brightness)
            if any(value is not None for value in supplied):
                if not all(value is not None for value in supplied):
                    raise ValueError("setting config requires all four configuration options")
                device.set_config(args.address, *supplied)
            current = device.config(args.address)
            print(f"address: 0x{current[0]:02x}")
            print(f"startup mask: 0x{current[1]:02x}")
            print(f"timeout: {current[2]} ms")
            print(f"timeout mask: 0x{current[3]:02x}")
            print(f"brightness: {current[4]}")
        elif args.command == "factory-reset":
            device.factory_reset(args.address)
            print(f"board {args.address} restored to defaults; address is now 0xff")
        elif args.command == "output":
            # Read-modify-write preserves every channel except the requested one.
            value = device.outputs(args.address)
            bit = 1 << (args.channel - 1)
            value = value | bit if args.state == "on" else value & ~bit
            show_outputs(device.set_outputs(args.address, value))
        elif args.command == "set-address":
            device.assign(args.uid, args.new_address)
            print(
                f"assigned {args.uid.hex()} to {args.new_address}"
            )
        elif args.command == "reset":
            device.reset(args.address)
            print(f"board {args.address} acknowledged reset")
        elif args.command == "simulate-error":
            print(f"sent bad-CRC request: {device.send_bad_crc(args.address).hex(' ')}")


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
