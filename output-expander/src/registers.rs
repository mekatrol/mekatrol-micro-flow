// Reuse the canonical configuration, output, and UID representations.
use crate::{config::Config, outputs::Outputs, uid};

/// Number of writable/readable configuration holding registers.

pub const HOLDING_COUNT: u16 = 5;

/// Three status registers plus the registers containing the complete UID.

pub const INPUT_COUNT: u16 = 3 + uid::UID_REGISTERS as u16;

/// Firmware version reported by input register zero.
///
/// Keep this explicit rather than parsing Cargo environment strings in the
/// firmware. Changing either byte is therefore a deliberate protocol change.
pub const FIRMWARE_VERSION: u16 = 0x0001;

/// The stored slave address is valid for normal Modbus unicast traffic.
pub const STATUS_CONFIGURED: u16 = 1 << 0;

/// The live output state has been forced to the configured timeout mask.
pub const STATUS_COMMS_TIMEOUT: u16 = 1 << 1;

/// At least one UART receive error has been observed since reset.
pub const STATUS_UART_ERROR: u16 = 1 << 2;

/// Persistent configuration could not be loaded or saved.
pub const STATUS_CONFIG_FAULT: u16 = 1 << 3;

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

            Some(FIRMWARE_VERSION)
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

#[cfg(test)]
mod tests {
    use super::*;

    fn bank() -> RegisterBank {
        let mut config = Config::default();
        config.slave_address = 17;
        config.startup_outputs = 0x12;
        config.comms_timeout_ms = 1234;
        config.timeout_outputs = 0x34;
        config.brightness = 56;

        let mut outputs = Outputs::new_off();
        outputs.set_mask(0xA5);

        RegisterBank {
            config,
            outputs,
            status: STATUS_CONFIGURED | STATUS_UART_ERROR,
            uid: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
        }
    }

    #[test]
    fn exposes_complete_holding_register_map() {
        let bank = bank();

        let expected = [17, 0x12, 1234, 0x34, 56];
        for (address, value) in expected.into_iter().enumerate() {
            assert_eq!(bank.holding(address as u16), Some(value));
        }
        assert_eq!(bank.holding(HOLDING_COUNT), None);
    }

    #[test]
    fn exposes_live_state_and_full_uid_in_order() {
        let bank = bank();

        assert_eq!(bank.input(0), Some(FIRMWARE_VERSION));
        assert_eq!(bank.input(1), Some(0xA5));
        assert_eq!(bank.input(2), Some(STATUS_CONFIGURED | STATUS_UART_ERROR));
        let expected_uid = [0x0001, 0x0203, 0x0405, 0x0607, 0x0809, 0x0A0B];
        for (index, value) in expected_uid.into_iter().enumerate() {
            assert_eq!(bank.input(3 + index as u16), Some(value));
        }
        assert_eq!(bank.input(INPUT_COUNT), None);
    }
}
