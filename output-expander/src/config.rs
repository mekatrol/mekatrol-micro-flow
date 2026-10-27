/// Fixed flash-record size, including reserved space and checksum.

pub const RECORD_LEN: usize = 64;

/// "MEK1" marker used to distinguish our record from erased/random flash.

pub const MAGIC: u32 = 0x4D45_4B31;

/// On-flash schema version; incompatible layouts must use a new value.

pub const VERSION: u16 = 1;

/// Reachable factory address used until persistent commissioning is available.
pub const FACTORY_ADDRESS: u8 = 247;

// Configuration is a compact value that can be copied and compared in tests.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Settings that survive power cycles after being stored in flash.

pub struct Config {
    /// Protocol address; factory-fresh devices use address 247.
    pub slave_address: u8,
    /// Output bit mask applied when firmware starts.
    pub startup_outputs: u8,
    /// Silence duration after which outputs enter their timeout state.
    pub comms_timeout_ms: u16,
    /// Output bit mask applied after a communications timeout.
    pub timeout_outputs: u8,
    /// Status LED intensity used to limit glare and current.
    pub brightness: u8,
}

// `Default` defines safe, usable settings for a blank device.
impl Default for Config {
    fn default() -> Self {
        // Start unaddressed with outputs off and a five-second timeout.
        Self {
            slave_address: FACTORY_ADDRESS,
            startup_outputs: 0,
            comms_timeout_ms: 5000,
            timeout_outputs: 0,
            brightness: 32,
        }
    }
}

impl Config {
    /// Check whether all fields that have restricted values are legal.

    pub const fn is_valid(&self) -> bool {
        self.slave_address >= 1 && self.slave_address <= 0xFE
    }

    /// Report whether the device has a usable unicast protocol address.

    pub const fn is_configured(&self) -> bool {
        // Only the standard slave-address range counts as commissioned.
        self.slave_address >= 1 && self.slave_address <= 0xFE
    }

    /// Serialise settings into the versioned, checksummed flash format.

    pub fn encode(&self) -> [u8; RECORD_LEN] {
        // Initialise unused bytes to erased-flash 0xFF for deterministic data.
        let mut out = [0xFF; RECORD_LEN];

        // Little endian matches the MCU and is fixed as part of this format.
        out[0..4].copy_from_slice(&MAGIC.to_le_bytes());

        // Store the schema version so later firmware can reject old layouts.
        out[4..6].copy_from_slice(&VERSION.to_le_bytes());

        // Single-byte fields need no byte-order conversion.
        out[6] = self.slave_address;

        // Store the startup output bit mask in its assigned byte.
        out[7] = self.startup_outputs;

        // Split the 16-bit timeout into two little-endian bytes.
        out[8..10].copy_from_slice(&self.comms_timeout_ms.to_le_bytes());

        // Store the timeout output state and LED brightness.
        out[10] = self.timeout_outputs;

        out[11] = self.brightness;

        // Protect all data and reserved bytes except the CRC field itself.
        let crc = crc32(&out[..60]);

        // Place the checksum in the record's final four bytes.
        out[60..64].copy_from_slice(&crc.to_le_bytes());

        // Return the complete stack-allocated flash record.
        out
    }

    /// Validate and decode one raw flash record.

    pub fn decode(raw: &[u8; RECORD_LEN]) -> Option<Self> {
        // `try_into` checks the slice length; `?` turns failure into `None`.
        // Reject records belonging to another format or schema version.
        if u32::from_le_bytes(raw[0..4].try_into().ok()?) != MAGIC
            || u16::from_le_bytes(raw[4..6].try_into().ok()?) != VERSION
        {
            return None;
        }

        // Require both a correct checksum and untouched 0xFF reserved bytes.
        // Reserved-byte validation catches corruption and future layouts.
        if u32::from_le_bytes(raw[60..64].try_into().ok()?) != crc32(&raw[..60])
            || raw[12..60].iter().any(|b| *b != 0xFF)
        {
            return None;
        }

        // Reassemble typed settings only after structural checks have passed.
        let value = Self {
            slave_address: raw[6],
            startup_outputs: raw[7],
            comms_timeout_ms: u16::from_le_bytes([raw[8], raw[9]]),
            timeout_outputs: raw[10],
            brightness: raw[11],
        };

        // Convert the boolean validation result into `Some` or `None`.
        value.is_valid().then_some(value)
    }
}

/// Compute the standard reflected CRC-32 used to detect record corruption.

pub fn crc32(bytes: &[u8]) -> u32 {
    // The algorithm begins with every accumulator bit set.
    let mut crc = 0xFFFF_FFFFu32;

    // Mix every input byte into the low end of the accumulator.
    for &byte in bytes {
        // Widen the byte before XOR so it matches the accumulator type.
        crc ^= byte as u32;

        // Process each of the byte's eight bits, least-significant first.
        for _ in 0..8 {
            // `wrapping_sub(bit)` becomes all ones when bit is one, selecting
            // the polynomial without a branch; otherwise it selects zero.
            crc = (crc >> 1) ^ (0xEDB8_8320 & (0u32.wrapping_sub(crc & 1)));
        }
    }

    // CRC-32 applies a final bitwise inversion.
    !crc
}

// Test code is excluded from the no-std firmware build.
#[cfg(test)]

mod tests {

    // Import the parent module's API for concise test expressions.
    use super::*;

    #[test]
    /// Check successful round-tripping and rejection after bit corruption.

    fn round_trip_and_corruption() {
        // Use the known-safe default as representative configuration data.
        let c = Config::default();

        // Encode into the exact bytes that would be written to flash.
        let mut r = c.encode();

        // A valid encoded value must decode without information loss.
        assert_eq!(Config::decode(&r), Some(c));

        // Flip one timeout bit without updating the checksum.
        r[8] ^= 1;

        // The CRC must make that corrupted record unusable.
        assert_eq!(Config::decode(&r), None);
    }
}
