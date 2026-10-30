# Output Expander RS-485 Protocol — Version 1

## 1. Purpose

A small, open, binary master/device protocol for CH32V003-based output expanders and related automation peripherals. The bus is controlled by the project's own controller or a gateway exposing a Python API to software such as Home Assistant. This document defines the production version-one contract.

### Firmware support

| Command | Production behavior |
|---|---|
| `PING` | Implemented |
| `GET_OUTPUTS` | Implemented |
| `SET_OUTPUTS` | Implemented |
| `GET_OUTPUT_DELAY` | Implemented |
| `SET_OUTPUT_DELAY` | Implemented; 500–10000 ms |
| `ASSIGN_ADDRESS_BY_UID` | Implemented with verified EEPROM persistence |
| `DISCOVER_DEVICES` | Implemented with randomized, collision-avoiding replies |
| `CANCEL_DISCOVERY` | Implemented; cancels replies still waiting to transmit |
| `RESET` | Implemented; acknowledgement is transmitted before watchdog reset |
| `GET_STATUS` | Implemented |
| `GET_CONFIG` | Implemented with EEPROM-backed configuration |
| `SET_CONFIG` | Implemented with verified transactional EEPROM commit |

The Python CLI exposes the commissioning, inspection, output-control, reset,
and diagnostic operations used by installers and service tools.

## 2. Physical layer and bus ownership

- Two-wire half-duplex RS-485, initially **19,200 baud, 8 data bits, no parity, 1 stop bit (8N1)**.
- A single master initiates all traffic. Devices normally transmit only in response to a valid unicast request addressed to them. `DISCOVER_DEVICES` is the deliberate exception described below: every reply still originates from one explicit master request.
- Broadcast requests receive **no response** except for the delayed, collision-avoiding `DISCOVER_DEVICES` replies. No unsolicited telemetry is permitted in v1.
- The master must wait for a response or timeout before sending the next request. A device must release DE only after USART transmission-complete (TC), not merely when the transmit register is empty.
- Bus termination, biasing, grounding and surge/ESD protection are hardware responsibilities. CRC does not substitute for proper wiring.

## 3. Framing

### What COBS means

COBS stands for **Consistent Overhead Byte Stuffing**. It is a reversible byte
encoding which guarantees that the encoded frame body contains no `0x00`
bytes. The encoder replaces each zero with a count describing the run of
non-zero bytes before the next zero. The decoder uses those counts to restore
the original zeros.

For example, this decoded data:

```text
11 22 00 33
```

is COBS-encoded as:

```text
03 11 22 02 33
```

The first `03` means that two data bytes follow before a zero must be restored;
the final `02` describes the remaining one-byte run. The exact encoded values
are less important to users than the resulting property: because the encoded
body can never contain zero, a real `0x00` byte can safely mark the end of every
frame.

This firmware uses COBS so framing does not depend on precise silent intervals
between UART characters. After noise, corruption, or dropped bytes, a receiver
can discard input through the next `0x00` delimiter and begin the following
frame at a known boundary. COBS provides framing only; CRC-16 separately detects
accidental corruption, and neither mechanism provides authentication or
encryption.

Each decoded frame contains the following fields, in order:

| Offset | Size | Field | Description |
|---|---:|---|---|
| 0 | 1 | `VERSION` | `0x01` |
| 1 | 1 | `DEST` | Device address; `0x00` = broadcast |
| 2 | 1 | `SEQ` | Master-assigned transaction sequence, modulo 256 |
| 3 | 1 | `TYPE` | `0x00` request, `0x01` response, `0x02` error |
| 4 | 1 | `CMD` | Command identifier |
| 5 | 1 | `LEN` | Payload length, 0–24 bytes |
| 6 | `LEN` | `PAYLOAD` | Command-specific bytes |
| 6+LEN | 2 | `CRC16` | Little-endian: low byte then high byte |

**Maximum decoded frame: 32 bytes** (6-byte header + 24-byte payload + 2-byte CRC). The entire decoded frame is COBS-encoded and terminated by a single `0x00` byte. COBS removes zero bytes from the encoded frame; `0x00` unambiguously marks its end. A receiver must reject frames with invalid COBS encoding, unexpected length, unsupported version, invalid type, excessive payload, or incorrect CRC. No outputs or persistent settings may change for a rejected frame.

The CRC is **CRC-16/CCITT-FALSE**: polynomial `0x1021`, initial value `0xFFFF`, no reflection, final XOR `0x0000`, calculated over bytes `VERSION` through the final payload byte. The standard test vector `123456789` yields `0x29B1`. CRC is for accidental corruption detection, **not authentication**.

Multi-byte integers in payloads are little-endian. The 96-bit UID is transmitted as its 12 raw MCU UID bytes in a fixed order documented by the device's UID module; both host and firmware must use exactly the same order.

## 4. Addressing and transaction rules

- `0x00`: broadcast. Only commands explicitly marked broadcast-capable may use it. Broadcasts do not reply except for the explicitly defined delayed discovery operation.
- `0x01..0xFE`: commissioned device addresses.
- `0xFF`: uncommissioned discovery address. Only `PING` is accepted, and only
  for isolated-device UID discovery; all other commands are ignored.
- A normal unicast response copies `DEST`, `SEQ` and `CMD` from the request. A discovery response instead uses the responder's current address as `DEST`, while preserving the broadcast request's `SEQ` and `CMD`. An error frame uses `TYPE=0x02` and a one-byte error-code payload.
- The master accepts a response only if address, sequence, command, CRC and response type are valid for its outstanding request. At most one request may be outstanding on the bus.
- A device processes a valid unicast request and sends at most one response. To avoid repeating side effects after a lost reply, the master should retry with the same `SEQ`; the device should cache the most recent transaction result for its own address when feasible. **Persistence commands must be idempotent**, regardless of whether a reply cache is implemented.
- Initial master response timeout: **100 ms**, configurable in the host. Initial retry count: **2**. These are host defaults, not guaranteed device timing limits; validate against EEPROM write latency and RS-485 turnaround.

## 5. Commands

| CMD | Name | Request payload | Success response payload | Broadcast? |
|---|---|---|---|---|
| `0x01` | `PING` | None | `UID[12]`, firmware major `u8`, minor `u8` | No |
| `0x02` | `GET_STATUS` | None | outputs `u8`, inputs `u8`, status `u16 LE` | No |
| `0x10` | `SET_OUTPUTS` | output mask `u8` | accepted target mask `u8` | No |
| `0x11` | `GET_OUTPUTS` | None | current mask `u8` | No |
| `0x12` | `GET_OUTPUT_DELAY` | None | activation delay `u8`, in 100 ms units | No |
| `0x13` | `SET_OUTPUT_DELAY` | activation delay `u8`, in 100 ms units | accepted delay `u8`, in 100 ms units | No |
| `0x20` | `GET_CONFIG` | None | address `u8`, startup mask `u8`, timeout `u16 LE`, timeout mask `u8`, brightness `u8` | No |
| `0x21` | `SET_CONFIG` | startup mask `u8`, timeout `u16 LE`, timeout mask `u8`, brightness `u8` | same 5 bytes on successful EEPROM commit | No |
| `0x22` | `FACTORY_RESET` | None | None | No |
| `0x30` | `ASSIGN_ADDRESS_BY_UID` | UID `[12]`, new address `u8` | None | Yes |
| `0x31` | `DISCOVER_DEVICES` | None | UID `[12]`, current address `u8` | Yes |
| `0x32` | `CANCEL_DISCOVERY` | None | None | Yes |
| `0x40` | `RESET` | None | None; see reset rule | No |

### Command details

**SET_OUTPUTS:** Bit 0 controls channel 1 and bit 7 controls channel 8. `1` = logical ON. Physical outputs are active LOW. The response confirms the complete target mask accepted by the activation sequencer; `GET_OUTPUTS` reports the mask currently applied to GPIO. OFF transitions occur immediately, while OFF-to-ON transitions are applied one channel at a time with the configured delay between them. A newer command replaces the complete target and cancels obsolete pending activations. This changes volatile output state only and **must not write EEPROM**.

**GET_OUTPUT_DELAY / SET_OUTPUT_DELAY:** The delay is one `u8` expressed in units of 100 ms. Values from 5 through 100 represent 500 ms through 10000 ms. Values outside that range are rejected with `BAD_VALUE` because the lower bound is a hardware-safety constraint and the upper bound defines this setting's supported operating range. The factory default is 5 (500 ms). The one-byte representation is stored directly in EEPROM. `SET_OUTPUT_DELAY` commits the new value using the verified transactional EEPROM configuration mechanism before activating it and acknowledging success.

**SET_CONFIG:** Validate every field before writing. Commit the new record to the inactive EEPROM slot, verify by readback, then activate it in RAM. Respond only after success. A failure leaves the last valid record active and returns `STORAGE_ERROR` if communication is still operational.

**FACTORY_RESET:** Commit safe defaults through the same redundant verified storage path, acknowledge the request on the old address, and then activate defaults. The defaults are address `0xFF`, startup/timeout masks zero, timeout disabled, LED brightness 30, and output activation delay 500 ms. The device is therefore ready for commissioning exactly as a new unit. This maintenance command is unauthenticated and must only be exposed on a trusted bus.

**ASSIGN_ADDRESS_BY_UID:** This command supports isolated-device commissioning. With exactly one uncommissioned device connected, send `PING` to `0xFF` to read its factory UID, then broadcast `ASSIGN_ADDRESS_BY_UID` with that UID and a new address in `0x01..0xFE`. Every device compares all 12 UID bytes; only the matching device commits the address to EEPROM and activates it after readback verification. The command does not assign or modify the UID. Because assignment is broadcast, **no device responds**. The master verifies assignment by sending `PING` to the new address and comparing the returned UID. Do not discover multiple uncommissioned devices together: they would all respond to `PING 0xFF` and collide. Readdressing an already commissioned unit is permitted on a trusted bus.

**DISCOVER_DEVICES / CANCEL_DISCOVERY:** The master broadcasts an empty `DISCOVER_DEVICES` request when it needs an inventory that does not depend on configured addresses. Every device records the request sequence and starts a deterministic pseudo-random delay of 10–100 ms. The random generator is seeded from the device's 96-bit UID, so different physical devices normally choose different slots without requiring an entropy peripheral. When the delay expires, the device sends one `DISCOVER_DEVICES` response whose `DEST` and final payload byte both contain its current address; the first 12 payload bytes contain its UID. Address `0xFF` is valid in this response and identifies an uncommissioned device.

All devices continue listening while their response is pending. When a device receives another device's discovery response with the active sequence, it waits for that complete frame to finish and starts a new 10–100 ms random delay. It does not require another master request. This listen-and-defer rule serializes replies after an initial tie; CRC or framing failures are not treated as successful peer replies. A device abandons the pending reply 15 seconds after the original request, regardless of how many times it deferred, so continuous traffic cannot keep discovery armed forever. An empty broadcast `CANCEL_DISCOVERY` immediately disarms every reply that has not already completed. Cancellation intentionally does not require the original sequence number, which lets a restarted master stop an earlier scan. Neither discovery command changes outputs, configuration, or the persistent address.

**RESET:** The device must finish transmitting a valid response before triggering reset or intentionally allowing the watchdog to expire. The successful response has an empty payload. This is an unauthenticated maintenance command and must only be exposed on a trusted bus.

## 6. Status bits

`GET_STATUS` returns a little-endian 16-bit mask:

| Bit | Meaning |
|---:|---|
| 0 | Commissioned address stored and validated |
| 1 | Communications timeout active |
| 2 | UART/framing/CRC fault observed |
| 3 | EEPROM configuration fault |
| 4 | Watchdog/reset-related diagnostic (if supported) |
| 5–15 | Reserved; send zero and ignore on receipt |

The `inputs` byte uses bit 0 for the active-low PC7 button (`1` = pressed). Other bits are reserved and zero.

## 7. Error codes

| Code | Name | Meaning |
|---|---|---|
| `0x01` | `UNKNOWN_COMMAND` | Command not implemented |
| `0x02` | `BAD_LENGTH` | Payload length invalid for command |
| `0x03` | `BAD_VALUE` | Field out of range |
| `0x04` | `STORAGE_ERROR` | EEPROM commit or verification failed |
| `0x05` | `BUSY` | Device temporarily cannot execute command |
| `0x06` | `UNSUPPORTED` | Command valid but unavailable on this device |

Bad CRC, malformed COBS, wrong address and unsupported frame version are **silently discarded**, to avoid replies to noise. Error replies are only for well-formed, valid-CRC unicast requests.

## 8. Communications timeout and fail-safe behavior

A valid addressed `SET_OUTPUTS` request or controller heartbeat refreshes the communications timeout. In v1, `PING` is also a heartbeat. Broadcast commissioning does not refresh it. A timeout of zero disables monitoring; otherwise, after the configured number of milliseconds without qualifying traffic, the device applies the configured fallback output mask. `GET_STATUS` reports the condition. A subsequent valid control request clears it and resumes normal control. The first timeout interval starts after boot configuration has been validated and the startup mask applied.

## 9. EEPROM configuration storage

The common layout uses the first 128 bytes of the 256-byte EEPROM. Two independent 64-byte versioned configuration records occupy `0x00..0x3F` and `0x40..0x7F`; each contains a sequence number and CRC-32. The output activation delay occupies one configuration byte in the same 100 ms units used on the wire. The remaining bytes through `0xFF` are reserved. Writes are limited to explicit configuration changes, use EEPROM page boundaries, and require ACK polling plus full readback verification. The previous valid slot is retained until the replacement is verified. A blank or invalid EEPROM results in safe defaults and an uncommissioned address. Development builds default to AT24C02 (one address byte, 8-byte pages); production builds select XBLW 24C02S (one address byte, 16-byte pages) with `OUTPUT_EXPANDER_EEPROM`. No runtime device probing is performed.

## 10. Reference examples and testing

The Python reference implementation exposes COBS encoding/decoding, CRC-16, frame parsing, timeouts, UID commissioning, configuration, and output commands. Interoperability coverage includes:

- CRC vector `123456789` → `0x29B1`; payloads containing zero bytes; maximum 32-byte decoded frames.
- Invalid COBS, truncated frames, incorrect CRC, excessive length, unsupported version and wrong destination.
- Noise, dropped bytes, UART overrun, repeated sequence numbers, late replies and bus contention.
- Eight output-bit transitions, uncommissioned device behavior, UID mismatch, duplicate addresses and reset acknowledgment.
- EEPROM blank/corrupt slots, interrupted writes, failed readback, sequence wraparound and repeated commits.
- RS-485 DE/TC timing and power-up output safety.

The host-side reference encoder/decoder and embedded parser use the same version-1 test vectors.
