// Reuse the canonical configuration, output, and UID representations.
use crate::{config::Config, outputs::Outputs, uid};

/// Number of writable/readable configuration holding registers.

pub const HOLDING_COUNT: u16 = 5;

/// Three status registers plus the registers containing the complete UID.

pub const INPUT_COUNT: u16 = 3 + uid::UID_REGISTERS as u16;

/// Values exposed through the device's Modbus register address space.

pub struct RegisterBank {
    /// Persistent settings represented by holding registers zero through four.
    pub config: Config,
    /// Live output state reported by input register one.
    pub outputs: Outputs,
    /// Firmware-defined health flags reported by input register two.
    pub status: u16,
    /// Immutable silicon identity spread over the remaining input registers.
    pub uid: [u8; uid::UID_BYTES],
}

impl RegisterBank {
    /// Read one holding register, or `None` when its address is not mapped.

    pub fn holding(&self, address: u16) -> Option<u16> {

        // Wrap the matched value in `Some`; an unknown arm returns early.
        Some(match address {
            // Widen byte-sized settings because Modbus registers are 16 bits.
            0 => self.config.slave_address as u16,
            1 => self.config.startup_outputs as u16,
            // The timeout already occupies a complete 16-bit register.
            2 => self.config.comms_timeout_ms,
            3 => self.config.timeout_outputs as u16,
            4 => self.config.brightness as u16,
            // `return` exits before the surrounding `Some` is constructed.
            _ => return None,
        })
    }

    /// Read one read-only input register, or `None` if it is unmapped.

    pub fn input(&self, address: u16) -> Option<u16> {

        // Register zero encodes firmware major and minor version as two bytes.
        if address == 0 {

            Some((0u16 << 8) | 1)
        } else if address == 1 {

            // Widen the eight output bits to the Modbus register width.
            Some(self.outputs.mask() as u16)
        } else if address == 2 {

            // Status is already stored as a 16-bit register value.
            Some(self.status)
        } else {

            // Convert the UID to words, translate address three to index zero,
            // and copy the selected word out if that index exists.
            uid::to_registers(self.uid)
                .get((address - 3) as usize)
                .copied()
        }
    }
}
