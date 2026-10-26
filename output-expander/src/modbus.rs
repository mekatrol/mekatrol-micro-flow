use crate::outputs::Outputs;

pub const MAX_RESPONSE: usize = 128;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub enum Exception {
    IllegalFunction = 1,
    IllegalDataAddress = 2,
    IllegalDataValue = 3,
    DeviceFailure = 4,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub enum Request<'a> {
    ReadCoils {
        start: u16,
        count: u16,
    },
    WriteCoil {
        address: u16,
        on: bool,
    },
    WriteCoils {
        start: u16,
        count: u16,
        data: &'a [u8],
    },
    ReadHolding {
        start: u16,
        count: u16,
    },
    ReadInput {
        start: u16,
        count: u16,
    },
}

pub fn crc16(bytes: &[u8]) -> u16 {

    let mut crc = 0xFFFFu16;

    for &b in bytes {

        crc ^= b as u16;

        for _ in 0..8 {

            crc = (crc >> 1) ^ (0xA001 & 0u16.wrapping_sub(crc & 1));
        }
    }

    crc
}

pub fn valid_crc(frame: &[u8]) -> bool {

    frame.len() >= 4
        && crc16(&frame[..frame.len() - 2])
            == u16::from_le_bytes([frame[frame.len() - 2], frame[frame.len() - 1]])
}

fn be16(data: &[u8]) -> u16 {

    u16::from_be_bytes([data[0], data[1]])
}

pub fn parse(frame: &[u8]) -> Result<Request<'_>, Exception> {

    if !valid_crc(frame) {

        return Err(Exception::DeviceFailure);
    }

    let p = &frame[2..frame.len() - 2];

    match frame[1] {
        0x01 | 0x03 | 0x04 if p.len() == 4 => {

            let start = be16(p);

            let count = be16(&p[2..]);

            if count == 0 {

                return Err(Exception::IllegalDataValue);
            }

            match frame[1] {
                0x01 => Ok(Request::ReadCoils { start, count }),
                0x03 => Ok(Request::ReadHolding { start, count }),
                _ => Ok(Request::ReadInput { start, count }),
            }
        }
        0x05 if p.len() == 4 => {

            let value = be16(&p[2..]);

            let on = match value {
                0xFF00 => true,
                0 => false,
                _ => return Err(Exception::IllegalDataValue),
            };

            Ok(Request::WriteCoil {
                address: be16(p),
                on,
            })
        }
        0x0F if p.len() >= 5 => {

            let start = be16(p);

            let count = be16(&p[2..]);

            let bytes = p[4] as usize;

            if count == 0 || bytes != (count as usize).div_ceil(8) || p.len() != 5 + bytes {

                return Err(Exception::IllegalDataValue);
            }

            Ok(Request::WriteCoils {
                start,
                count,
                data: &p[5..],
            })
        }
        0x01 | 0x03 | 0x04 | 0x05 | 0x0F => Err(Exception::IllegalDataValue),
        _ => Err(Exception::IllegalFunction),
    }
}

pub fn apply_coils(request: Request<'_>, outputs: &mut Outputs) -> Result<(), Exception> {

    match request {
        Request::WriteCoil { address, on } => {

            if address >= 8 {

                return Err(Exception::IllegalDataAddress);
            }

            outputs
                .set_channel(address as u8, on)
                .map_err(|_| Exception::IllegalDataAddress)
        }
        Request::WriteCoils { start, count, data } => {

            if start >= 8 || count > 8 || start + count > 8 {

                return Err(Exception::IllegalDataAddress);
            }

            let mut mask = outputs.mask();

            for n in 0..count {

                let bit = 1u8 << (start + n);

                if data[(n / 8) as usize] & (1 << (n % 8)) != 0 {

                    mask |= bit
                } else {

                    mask &= !bit
                }
            }

            outputs.set_mask(mask);

            Ok(())
        }
        _ => Err(Exception::IllegalFunction),
    }
}

pub fn append_crc(buffer: &mut [u8], payload_len: usize) -> Option<usize> {

    if payload_len + 2 > buffer.len() {

        return None;
    }

    let crc = crc16(&buffer[..payload_len]).to_le_bytes();

    buffer[payload_len..payload_len + 2].copy_from_slice(&crc);

    Some(payload_len + 2)
}

#[cfg(test)]

mod tests {

    use super::*;

    fn framed(payload: [u8; 6]) -> [u8; 8] {

        let mut out = [0; 8];

        out[..6].copy_from_slice(&payload);

        let c = crc16(&payload).to_le_bytes();

        out[6..].copy_from_slice(&c);

        out
    }

    #[test]

    fn standard_crc_vector() {

        assert_eq!(crc16(b"123456789"), 0x4B37)
    }

    #[test]

    fn write_single_coil() {

        let f = framed([1, 5, 0, 0, 0xFF, 0]);

        let r = parse(&f).unwrap();

        let mut o = Outputs::new_off();

        apply_coils(r, &mut o).unwrap();

        assert_eq!(o.mask(), 1)
    }

    #[test]

    fn rejects_bad_crc() {

        let mut f = framed([1, 1, 0, 0, 0, 8]);

        f[2] ^= 1;

        assert!(parse(&f).is_err())
    }
}
