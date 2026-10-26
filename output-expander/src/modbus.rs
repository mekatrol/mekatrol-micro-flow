// Output write requests are applied through the shared logical output model.
use crate::outputs::Outputs;

/// Largest response that callers need to reserve, including its CRC.

pub const MAX_RESPONSE: usize = 128;

// Explicit numeric values are the exception codes transmitted by Modbus.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Standard Modbus error categories returned for invalid requests.

pub enum Exception {
    /// The requested function code is not implemented by this device.
    IllegalFunction = 1,
    /// A coil or register address lies outside the implemented map.
    IllegalDataAddress = 2,
    /// The message shape or requested value is invalid.
    IllegalDataValue = 3,
    /// The frame cannot be trusted, for example because its CRC is wrong.
    DeviceFailure = 4,
}

// Requests borrow frame data where possible, avoiding copies and allocation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Validated operation decoded from a Modbus RTU frame.

pub enum Request<'a> {
    /// Read a consecutive range of one-bit output coils.
    ReadCoils {
        /// Address of the first requested coil.
        start: u16,
        /// Number of consecutive coils to return.
        count: u16,
    },
    /// Change one output coil.
    WriteCoil {
        /// Zero-based coil address.
        address: u16,
        /// `true` energises the coil; `false` clears it.
        on: bool,
    },
    /// Change several packed output coils.
    WriteCoils {
        /// Address corresponding to bit zero of the first data byte.
        start: u16,
        /// Number of meaningful packed bits.
        count: u16,
        /// Borrowed packed bits from the original frame payload.
        data: &'a [u8],
    },
    /// Read persistent configuration holding registers.
    ReadHolding { start: u16, count: u16 },
    /// Read live, read-only input registers.
    ReadInput { start: u16, count: u16 },
}

/// Calculate the reflected Modbus CRC-16 checksum for a byte slice.

pub fn crc16(bytes: &[u8]) -> u16 {

    // Modbus specifies an all-ones initial accumulator.
    let mut crc = 0xFFFFu16;

    // Incorporate each byte in transmitted order.
    for &b in bytes {

        // XOR the next byte into the accumulator's least-significant byte.
        crc ^= b as u16;

        // Shift and conditionally apply the polynomial for all eight bits.
        for _ in 0..8 {

            // Subtracting the low bit from zero creates either 0x0000 or
            // 0xFFFF, selecting the polynomial without a branch.
            crc = (crc >> 1) ^ (0xA001 & 0u16.wrapping_sub(crc & 1));
        }
    }

    // Modbus transmits this value low byte first.
    crc
}

/// Check that a complete frame has a minimum length and matching CRC bytes.

pub fn valid_crc(frame: &[u8]) -> bool {

    // Short-circuit the length check before subtracting or indexing. The final
    // two bytes contain the little-endian CRC; all earlier bytes are protected.
    frame.len() >= 4
        && crc16(&frame[..frame.len() - 2])
            == u16::from_le_bytes([frame[frame.len() - 2], frame[frame.len() - 1]])
}

/// Read a two-byte, big-endian Modbus field.

fn be16(data: &[u8]) -> u16 {

    // Modbus multi-byte data fields put the most-significant byte first.
    u16::from_be_bytes([data[0], data[1]])
}

/// Validate a full RTU frame and decode its supported request operation.

pub fn parse(frame: &[u8]) -> Result<Request<'_>, Exception> {

    // Reject corrupted data before trusting its function code or indexes.
    if !valid_crc(frame) {

        return Err(Exception::DeviceFailure);
    }

    // Exclude slave address, function code, and trailing two-byte CRC.
    let p = &frame[2..frame.len() - 2];

    // Function code selects both payload layout and resulting request variant.
    match frame[1] {
        // These three read functions share start-address and count fields.
        0x01 | 0x03 | 0x04 if p.len() == 4 => {

            // Decode the first big-endian field as the starting address.
            let start = be16(p);

            // Decode the second field from the remaining payload bytes.
            let count = be16(&p[2..]);

            // Reading zero values has no useful meaning and violates Modbus.
            if count == 0 {

                return Err(Exception::IllegalDataValue);
            }

            // Preserve the distinct function while reusing the shared decoder.
            match frame[1] {
                0x01 => Ok(Request::ReadCoils { start, count }),
                0x03 => Ok(Request::ReadHolding { start, count }),
                // The guarded outer match means the remaining value is 0x04.
                _ => Ok(Request::ReadInput { start, count }),
            }
        }
        // Function 0x05 writes one coil using a prescribed 16-bit value.
        0x05 if p.len() == 4 => {

            // The second word holds the encoded on/off value.
            let value = be16(&p[2..]);

            // Modbus uses 0xFF00 for on and 0x0000 for off; reject all others.
            let on = match value {
                0xFF00 => true,
                0 => false,
                _ => return Err(Exception::IllegalDataValue),
            };

            // The first payload word is the target coil address.
            Ok(Request::WriteCoil {
                address: be16(p),
                on,
            })
        }
        // Function 0x0F has a five-byte header followed by packed coil bits.
        0x0F if p.len() >= 5 => {

            // Decode target range and advertised byte count.
            let start = be16(p);

            let count = be16(&p[2..]);

            // Widen the byte-count field for safe comparison with slice length.
            let bytes = p[4] as usize;

            // Require a non-empty range, exactly enough bytes for its bits, and
            // no missing or trailing payload data.
            if count == 0 || bytes != (count as usize).div_ceil(8) || p.len() != 5 + bytes {

                return Err(Exception::IllegalDataValue);
            }

            // Borrow packed data after the five-byte header without copying it.
            Ok(Request::WriteCoils {
                start,
                count,
                data: &p[5..],
            })
        }
        // A known function with the wrong payload shape has an illegal value.
        0x01 | 0x03 | 0x04 | 0x05 | 0x0F => Err(Exception::IllegalDataValue),
        // Any other function is not supported by this device.
        _ => Err(Exception::IllegalFunction),
    }
}

/// Apply a validated coil-write request to the logical output state.

pub fn apply_coils(request: Request<'_>, outputs: &mut Outputs) -> Result<(), Exception> {

    // Reject read requests here; this routine deliberately handles writes only.
    match request {
        Request::WriteCoil { address, on } => {

            // The expander implements only coil addresses zero through seven.
            if address >= 8 {

                return Err(Exception::IllegalDataAddress);
            }

            // Narrowing is safe after the range check; translate the output
            // model's small unit error into the public Modbus exception type.
            outputs
                .set_channel(address as u8, on)
                .map_err(|_| Exception::IllegalDataAddress)
        }
        Request::WriteCoils { start, count, data } => {

            // Ensure both ends of the requested range remain within 8 outputs.
            if start >= 8 || count > 8 || start + count > 8 {

                return Err(Exception::IllegalDataAddress);
            }

            // Begin from the existing mask so channels outside the range remain.
            let mut mask = outputs.mask();

            // Visit each requested bit in ascending Modbus coil order.
            for n in 0..count {

                // Map the relative request position onto an output mask bit.
                let bit = 1u8 << (start + n);

                // Modbus packs coils least-significant bit first within bytes.
                if data[(n / 8) as usize] & (1 << (n % 8)) != 0 {

                    // A set request bit turns the corresponding output on.
                    mask |= bit
                } else {

                    // A clear request bit turns it off without changing others.
                    mask &= !bit
                }
            }

            // Commit once, after the entire validated range has been processed.
            outputs.set_mask(mask);

            // Report that the state update completed.
            Ok(())
        }
        // Read operations do not belong in the write-application path.
        _ => Err(Exception::IllegalFunction),
    }
}

/// Append a little-endian Modbus CRC to an existing response payload.

pub fn append_crc(buffer: &mut [u8], payload_len: usize) -> Option<usize> {

    // Reserve two bytes and reject callers whose buffer is too small.
    if payload_len + 2 > buffer.len() {

        return None;
    }

    // Calculate over payload bytes only and split into wire-order bytes.
    let crc = crc16(&buffer[..payload_len]).to_le_bytes();

    // Copy both CRC bytes immediately after the payload.
    buffer[payload_len..payload_len + 2].copy_from_slice(&crc);

    // Return the total frame length so the caller can transmit that slice.
    Some(payload_len + 2)
}

// Unit-test helpers and cases are excluded from production firmware.
#[cfg(test)]

mod tests {

    // Import private helpers as well as the public parent API.
    use super::*;

    /// Add a valid CRC to a six-byte test payload.

    fn framed(payload: [u8; 6]) -> [u8; 8] {

        // Allocate exact fixed storage for payload plus two CRC bytes.
        let mut out = [0; 8];

        // Put the caller's data at the start of the frame.
        out[..6].copy_from_slice(&payload);

        // Calculate and encode the checksum in Modbus byte order.
        let c = crc16(&payload).to_le_bytes();

        // Fill the remaining two positions with the checksum.
        out[6..].copy_from_slice(&c);

        // Return a ready-to-parse frame.
        out
    }

    #[test]
    /// Compare with the widely used CRC-16/Modbus reference vector.

    fn standard_crc_vector() {

        assert_eq!(crc16(b"123456789"), 0x4B37)
    }

    #[test]
    /// Decode and apply a valid single-coil write request.

    fn write_single_coil() {

        // Address one, function five, coil zero, and the on value 0xFF00.
        let f = framed([1, 5, 0, 0, 0xFF, 0]);

        // Parsing should succeed because the helper supplied a matching CRC.
        let r = parse(&f).unwrap();

        // Start from all outputs off so the expected result is unambiguous.
        let mut o = Outputs::new_off();

        // Apply the parsed write; unwrap makes unexpected failure fail the test.
        apply_coils(r, &mut o).unwrap();

        // Only channel zero, the least-significant bit, should be on.
        assert_eq!(o.mask(), 1)
    }

    #[test]
    /// Ensure a changed protected byte makes the frame invalid.

    fn rejects_bad_crc() {

        // Construct a valid read-eight-coils request first.
        let mut f = framed([1, 1, 0, 0, 0, 8]);

        // Corrupt one address byte without recalculating the checksum.
        f[2] ^= 1;

        // The parser must reject data whose received and computed CRCs differ.
        assert!(parse(&f).is_err())
    }
}
