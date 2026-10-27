//! Bounded framing for the output-expander protocol.
//!
//! This module owns only wire representation. It deliberately does not execute
//! commands, touch hardware, or allocate memory, which makes malformed-frame
//! handling deterministic and allows the exact same vectors to run on a host.
//!
//! COBS means **Consistent Overhead Byte Stuffing**. It transforms an arbitrary
//! packet into an encoded body containing no `0x00` bytes. Instead of copying a
//! zero, the encoder writes a small count telling the decoder how many non-zero
//! bytes occur before that zero. For example, decoded bytes `11 22 00 33` become
//! `03 11 22 02 33`: `03` describes the first two data bytes plus the suppressed
//! zero, and `02` describes the final byte. Firmware can therefore reserve an
//! actual `0x00` byte as an unmistakable end-of-frame delimiter. If bytes are
//! dropped or corrupted, the receiver discards through the next zero and starts
//! the following packet at a known boundary; no timing gap is needed.

/// Protocol version carried in every decoded packet.
pub const VERSION: u8 = 1;
/// Bytes occupied by version, address, sequence, type, command, and length.
pub const HEADER_LEN: usize = 6;
/// CRC-16 is transmitted as two little-endian bytes.
pub const CRC_LEN: usize = 2;
/// Payload limit from the version-one wire specification.
pub const MAX_PAYLOAD: usize = 24;
/// Largest packet after COBS decoding, including header and CRC.
pub const MAX_DECODED: usize = HEADER_LEN + MAX_PAYLOAD + CRC_LEN;
/// Largest COBS representation plus its trailing zero delimiter.
pub const MAX_WIRE: usize = MAX_DECODED + 2;

/// A command sent by the bus master.
pub const TYPE_REQUEST: u8 = 0;
/// A successful device reply.
pub const TYPE_RESPONSE: u8 = 1;
/// A device reply whose payload contains a protocol error code.
pub const TYPE_ERROR: u8 = 2;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Reasons an encoded frame cannot safely be passed to the dispatcher.
pub enum DecodeError {
    /// COBS structure is malformed or exceeds the fixed output buffer.
    Cobs,
    /// The decoded byte count disagrees with the declared payload length.
    Length,
    /// The sender uses a protocol version this firmware does not understand.
    Version,
    /// The packet type is outside request, response, and error.
    Type,
    /// The received CRC does not protect the decoded packet contents.
    Crc,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// One validated decoded packet with payload storage owned by the value.
pub struct Packet {
    /// Bus address copied into responses so the master can match a transaction.
    pub destination: u8,
    /// Master-selected sequence number copied into the response.
    pub sequence: u8,
    /// Request, response, or error packet type.
    pub kind: u8,
    /// Command identifier defined by `docs/protocol.md`.
    pub command: u8,
    // A fixed array avoids a heap and makes the maximum SRAM cost explicit.
    payload: [u8; MAX_PAYLOAD],
    // Only this prefix of `payload` belongs to the packet.
    payload_len: u8,
}

impl Packet {
    pub fn new(
        destination: u8,
        sequence: u8,
        kind: u8,
        command: u8,
        payload: &[u8],
    ) -> Option<Self> {
        // Reject values which cannot be represented by protocol version one.
        // Checking before copying prevents both truncation and an invalid type
        // from entering the trusted `Packet` representation.
        if payload.len() > MAX_PAYLOAD || kind > TYPE_ERROR {
            return None;
        }

        // Copy the caller's temporary slice into packet-owned storage so a
        // queued response never borrows a stack buffer that has gone away.
        let mut stored = [0; MAX_PAYLOAD];
        stored[..payload.len()].copy_from_slice(payload);
        Some(Self {
            destination,
            sequence,
            kind,
            command,
            payload: stored,
            payload_len: payload.len() as u8,
        })
    }

    pub fn payload(&self) -> &[u8] {
        // Hide unused capacity so command handlers cannot mistake zero-filled
        // spare bytes for payload received from the wire.
        &self.payload[..self.payload_len as usize]
    }
}

/// Calculate CRC-16/CCITT-FALSE over decoded header and payload bytes.
pub fn crc16(bytes: &[u8]) -> u16 {
    // CCITT-FALSE begins with all accumulator bits set and processes the most
    // significant bit first using polynomial 0x1021.
    let mut crc = 0xffff;
    for &byte in bytes {
        // Align each input byte with the high end of the 16-bit accumulator.
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            // Apply the polynomial only when the bit shifted out was one.
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Encode one packet into COBS bytes followed by the `0x00` frame delimiter.
pub fn encode(packet: &Packet, out: &mut [u8; MAX_WIRE]) -> usize {
    let payload = packet.payload();
    let decoded_len = HEADER_LEN + payload.len() + CRC_LEN;

    // Assemble the checksum domain in a bounded scratch buffer. Keeping this
    // representation explicit makes the documented byte offsets auditable.
    let mut decoded = [0; MAX_DECODED];
    decoded[0] = VERSION;
    decoded[1] = packet.destination;
    decoded[2] = packet.sequence;
    decoded[3] = packet.kind;
    decoded[4] = packet.command;
    decoded[5] = payload.len() as u8;
    decoded[6..6 + payload.len()].copy_from_slice(payload);

    // The wire format stores the CCITT value low byte first even though the
    // checksum algorithm itself processes bits most-significant first.
    let crc = crc16(&decoded[..decoded_len - CRC_LEN]).to_le_bytes();
    decoded[decoded_len - 2..decoded_len].copy_from_slice(&crc);
    let encoded_len = cobs_encode(&decoded[..decoded_len], out);

    // COBS guarantees the encoded body contains no zero, so this byte is an
    // unambiguous boundary even after dropped bytes or arbitrary line noise.
    out[encoded_len] = 0;
    encoded_len + 1
}

/// Decode and validate a COBS body which does not include its zero delimiter.
pub fn decode(encoded: &[u8]) -> Result<Packet, DecodeError> {
    // Decode into fixed storage first. No field is trusted or acted upon until
    // structural checks and the checksum have all succeeded.
    let mut decoded = [0; MAX_DECODED];
    let len = cobs_decode(encoded, &mut decoded)?;
    if !(HEADER_LEN + CRC_LEN..=MAX_DECODED).contains(&len) {
        return Err(DecodeError::Length);
    }

    // Unsupported versions are silently discarded by the caller because a
    // response using misunderstood semantics could make bus noise worse.
    if decoded[0] != VERSION {
        return Err(DecodeError::Version);
    }
    if decoded[3] > TYPE_ERROR {
        return Err(DecodeError::Type);
    }
    let payload_len = decoded[5] as usize;

    // Require the physical length to agree exactly with LEN. This rejects
    // truncated frames and also prevents trailing bytes from being ignored.
    if payload_len > MAX_PAYLOAD || len != HEADER_LEN + payload_len + CRC_LEN {
        return Err(DecodeError::Length);
    }
    let expected = u16::from_le_bytes([decoded[len - 2], decoded[len - 1]]);

    // CRC covers version through the final payload byte, never the CRC itself.
    if crc16(&decoded[..len - 2]) != expected {
        return Err(DecodeError::Crc);
    }
    Packet::new(
        decoded[1],
        decoded[2],
        decoded[3],
        decoded[4],
        &decoded[6..6 + payload_len],
    )
    .ok_or(DecodeError::Length)
}

fn cobs_encode(input: &[u8], output: &mut [u8]) -> usize {
    // Reserve the first output byte for the length code describing the first
    // run of non-zero input bytes.
    let mut read = 0;
    let mut write = 1;
    let mut code_index = 0;
    let mut code = 1u8;
    while read < input.len() {
        if input[read] == 0 {
            // Finish the current run and start a new one after the zero. The
            // zero itself is represented implicitly by the run boundary.
            output[code_index] = code;
            code_index = write;
            write += 1;
            code = 1;
        } else {
            // Ordinary non-zero bytes can be copied verbatim.
            output[write] = input[read];
            write += 1;
            code += 1;
            if code == 0xff {
                // COBS limits one run to 254 data bytes. This branch is not
                // reached by current packets but keeps the codec complete.
                output[code_index] = code;
                code_index = write;
                write += 1;
                code = 1;
            }
        }
        read += 1;
    }
    output[code_index] = code;
    write
}

fn cobs_decode(input: &[u8], output: &mut [u8]) -> Result<usize, DecodeError> {
    let mut read = 0;
    let mut write = 0;
    while read < input.len() {
        let code = input[read] as usize;
        // A zero code is forbidden, and a run may not point beyond the input.
        if code == 0 || read + code > input.len() + 1 {
            return Err(DecodeError::Cobs);
        }
        read += 1;
        for _ in 1..code {
            // Check both source and destination bounds before every copy so a
            // malformed frame cannot panic or overwrite adjacent state.
            if read >= input.len() || write >= output.len() {
                return Err(DecodeError::Cobs);
            }
            output[write] = input[read];
            write += 1;
            read += 1;
        }
        if code != 0xff && read < input.len() {
            // Restore the zero suppressed between two COBS runs. Do not append
            // one after the final run because it was not decoded packet data.
            if write >= output.len() {
                return Err(DecodeError::Cobs);
            }
            output[write] = 0;
            write += 1;
        }
    }
    Ok(write)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Result of feeding one UART byte into the foreground frame collector.
pub enum ReceiveResult {
    /// More bytes are needed before a decision can be made.
    None,
    /// A delimiter completed a valid, fully checked packet.
    Frame(Packet),
    /// A delimiter completed a malformed or previously corrupted frame.
    Rejected,
}

/// Fixed-capacity collector for one COBS frame at a time.
pub struct FrameReceiver {
    // The delimiter is consumed rather than stored, hence `MAX_WIRE - 1`.
    encoded: [u8; MAX_WIRE - 1],
    len: usize,
    // Once corruption is known, ignore bytes until the next delimiter. This
    // provides deterministic resynchronisation without interpreting a suffix.
    discard: bool,
}

impl FrameReceiver {
    pub const fn new() -> Self {
        Self {
            encoded: [0; MAX_WIRE - 1],
            len: 0,
            discard: false,
        }
    }

    /// Invalidate the partial frame after a UART error or queue overflow.
    pub fn reject_current(&mut self) {
        // Discard accumulated bytes now, then stay in discard mode until a
        // delimiter proves the following byte begins a fresh frame.
        self.len = 0;
        self.discard = true;
    }

    /// Consume one byte and publish a packet only after its delimiter arrives.
    pub fn push(&mut self, byte: u8) -> ReceiveResult {
        if byte == 0 {
            // A delimiter ends both valid and discarded frames. Empty frames
            // are harmless idle delimiters and therefore are not faults.
            if self.discard || self.len == 0 {
                let rejected = self.discard;
                self.len = 0;
                self.discard = false;
                return if rejected {
                    ReceiveResult::Rejected
                } else {
                    ReceiveResult::None
                };
            }
            let result = decode(&self.encoded[..self.len])
                .map_or(ReceiveResult::Rejected, ReceiveResult::Frame);
            // Release the buffer immediately so the next bus transaction can
            // begin even if the caller spends time dispatching this packet.
            self.len = 0;
            result
        } else if self.discard {
            // Do not fill RAM with a frame already known to be unusable.
            ReceiveResult::None
        } else if self.len == self.encoded.len() {
            // Never truncate an oversize frame into something that might pass
            // CRC by accident; reject the whole stream through its delimiter.
            self.reject_current();
            ReceiveResult::None
        } else {
            // Store ordinary encoded bytes in arrival order for later COBS
            // decoding in the foreground, outside interrupt context.
            self.encoded[self.len] = byte;
            self.len += 1;
            ReceiveResult::None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ccitt_reference_vector() {
        assert_eq!(crc16(b"123456789"), 0x29b1);
    }
    #[test]
    fn round_trip_payload_with_zeroes() {
        let p = Packet::new(7, 9, TYPE_REQUEST, 0x21, &[0, 1, 0, 2]).unwrap();
        let mut wire = [0; MAX_WIRE];
        let n = encode(&p, &mut wire);
        assert_eq!(wire[n - 1], 0);
        assert!(!wire[..n - 1].contains(&0));
        assert_eq!(decode(&wire[..n - 1]), Ok(p));
    }
    #[test]
    fn receiver_recovers_after_oversize_frame() {
        let mut r = FrameReceiver::new();
        for _ in 0..MAX_WIRE + 2 {
            assert_eq!(r.push(1), ReceiveResult::None);
        }
        assert_eq!(r.push(0), ReceiveResult::Rejected);
        let p = Packet::new(1, 1, TYPE_REQUEST, 1, &[]).unwrap();
        let mut wire = [0; MAX_WIRE];
        let n = encode(&p, &mut wire);
        let mut got = ReceiveResult::None;
        for &b in &wire[..n] {
            got = r.push(b);
        }
        assert_eq!(got, ReceiveResult::Frame(p));
    }
}
