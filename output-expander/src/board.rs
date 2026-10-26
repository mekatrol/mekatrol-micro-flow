//! Board-independent pin contract. The concrete pins remain in `main.rs` until
//! the production PCB is confirmed.

pub const OUTPUT_COUNT: usize = 8;

pub const RS485_BAUD: u32 = 19_200;

pub const RS485_DATA_BITS: u8 = 8;

pub const RS485_EVEN_PARITY: bool = true;

pub const RS485_STOP_BITS: u8 = 1;

/// Logical channel order from the provisional plan; not a physical-pin claim.

pub const PROVISIONAL_OUTPUT_SIGNALS: [&str; OUTPUT_COUNT] =
    ["PC0", "PC3", "PC4", "PC5", "PD0", "PD2", "PD3", "PD4"];
