//! Board-independent hardware and communication constants.
//!
//! Keeping these values together gives the rest of the program a stable
//! contract while the production PCB pinout is still being finalised.

/// Number of individually controlled digital outputs on the expander.

pub const OUTPUT_COUNT: usize = 8;

/// RS-485 line speed, measured in bits per second.

pub const RS485_BAUD: u32 = 19_200;

/// Number of useful data bits carried by each serial character.

pub const RS485_DATA_BITS: u8 = 8;

/// Whether serial parity is even, as required by the selected Modbus format.

pub const RS485_EVEN_PARITY: bool = true;

/// Number of stop bits marking the end of each serial character.

pub const RS485_STOP_BITS: u8 = 1;

/// Provisional MCU signal assigned to each logical output channel.
///
/// Array index zero represents output zero, and so on. The strings document
/// the current plan; they do not themselves configure physical pins.

pub const PROVISIONAL_OUTPUT_SIGNALS: [&str; OUTPUT_COUNT] =
    ["PC0", "PC3", "PC4", "PC5", "PD0", "PD2", "PD3", "PD4"];

/// Electrical polarity used by the current triac/FET board bring-up.
///
/// A logical output is active when its MCU pin is LOW. This matches the
/// successfully tested PC0 channel. The production schematic must confirm
/// that all eight channels use the same polarity before release.
pub const OUTPUTS_ACTIVE_LOW: bool = true;

/// Convert a logical output bit into the electrical GPIO-high state.
///
/// Keeping this conversion hardware-independent makes the channel ordering
/// and polarity testable on the host.
pub const fn output_pin_is_high(mask: u8, channel: usize) -> Option<bool> {
    if channel >= OUTPUT_COUNT {
        return None;
    }

    let logical_on = mask & (1u8 << channel) != 0;
    Some(if OUTPUTS_ACTIVE_LOW {
        !logical_on
    } else {
        logical_on
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn output_bits_map_to_active_low_pins() {
        for channel in 0..OUTPUT_COUNT {
            assert_eq!(output_pin_is_high(0, channel), Some(true));
            assert_eq!(output_pin_is_high(1u8 << channel, channel), Some(false));
        }

        assert_eq!(output_pin_is_high(0, OUTPUT_COUNT), None);
    }
}
