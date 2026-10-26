pub const RECORD_LEN: usize = 64;

pub const MAGIC: u32 = 0x4D45_4B31;

pub const VERSION: u16 = 1;

pub const UNCONFIGURED: u8 = 0xFF;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub struct Config {
    pub slave_address: u8,
    pub startup_outputs: u8,
    pub comms_timeout_ms: u16,
    pub timeout_outputs: u8,
    pub brightness: u8,
}

impl Default for Config {
    fn default() -> Self {

        Self {
            slave_address: UNCONFIGURED,
            startup_outputs: 0,
            comms_timeout_ms: 5000,
            timeout_outputs: 0,
            brightness: 32,
        }
    }
}

impl Config {
    pub const fn is_valid(&self) -> bool {

        self.slave_address == UNCONFIGURED || (self.slave_address >= 1 && self.slave_address <= 247)
    }

    pub const fn is_configured(&self) -> bool {

        self.slave_address >= 1 && self.slave_address <= 247
    }

    pub fn encode(&self) -> [u8; RECORD_LEN] {

        let mut out = [0xFF; RECORD_LEN];

        out[0..4].copy_from_slice(&MAGIC.to_le_bytes());

        out[4..6].copy_from_slice(&VERSION.to_le_bytes());

        out[6] = self.slave_address;

        out[7] = self.startup_outputs;

        out[8..10].copy_from_slice(&self.comms_timeout_ms.to_le_bytes());

        out[10] = self.timeout_outputs;

        out[11] = self.brightness;

        let crc = crc32(&out[..60]);

        out[60..64].copy_from_slice(&crc.to_le_bytes());

        out
    }

    pub fn decode(raw: &[u8; RECORD_LEN]) -> Option<Self> {

        if u32::from_le_bytes(raw[0..4].try_into().ok()?) != MAGIC
            || u16::from_le_bytes(raw[4..6].try_into().ok()?) != VERSION
        {

            return None;
        }

        if u32::from_le_bytes(raw[60..64].try_into().ok()?) != crc32(&raw[..60])
            || raw[12..60].iter().any(|b| *b != 0xFF)
        {

            return None;
        }

        let value = Self {
            slave_address: raw[6],
            startup_outputs: raw[7],
            comms_timeout_ms: u16::from_le_bytes([raw[8], raw[9]]),
            timeout_outputs: raw[10],
            brightness: raw[11],
        };

        value.is_valid().then_some(value)
    }
}

pub fn crc32(bytes: &[u8]) -> u32 {

    let mut crc = 0xFFFF_FFFFu32;

    for &byte in bytes {

        crc ^= byte as u32;

        for _ in 0..8 {

            crc = (crc >> 1) ^ (0xEDB8_8320 & (0u32.wrapping_sub(crc & 1)));
        }
    }

    !crc
}

#[cfg(test)]

mod tests {

    use super::*;

    #[test]

    fn round_trip_and_corruption() {

        let c = Config::default();

        let mut r = c.encode();

        assert_eq!(Config::decode(&r), Some(c));

        r[8] ^= 1;

        assert_eq!(Config::decode(&r), None);
    }
}
