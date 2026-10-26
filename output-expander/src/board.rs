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
