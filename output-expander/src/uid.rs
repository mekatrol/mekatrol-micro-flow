/// The CH32 chip exposes a 96-bit, or 12-byte, unique identifier.

pub const UID_BYTES: usize = 12;

/// Six 16-bit Modbus registers are required to carry all 12 UID bytes.

pub const UID_REGISTERS: usize = 6;

/// Convert the UID into big-endian Modbus words without losing any bits.

pub fn to_registers(uid: [u8; UID_BYTES]) -> [u16; UID_REGISTERS] {

    // Allocate the fixed-size result on the stack; embedded code avoids a heap.
    let mut out = [0; UID_REGISTERS];

    // A mutable index permits a `while` loop in constrained/no-std contexts.
    let mut i = 0;

    // Consume two UID bytes for each output register.
    while i < UID_REGISTERS {

        // Modbus sends register values most-significant byte first.
        out[i] = u16::from_be_bytes([uid[i * 2], uid[i * 2 + 1]]);

        // Advance to the next non-overlapping byte pair and register.
        i += 1;
    }

    // Return the fully populated array by value.
    out
}

// The hardware UID peripheral exists only in the actual bare-metal target.
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
/// Read the immutable silicon UID from the microcontroller.

pub fn read() -> [u8; UID_BYTES] {

    // The HAL hides the chip-specific memory addresses and volatile access.
    ch32_hal::signature::unique_id()
}

// Host-side unit tests do not need or have access to the hardware UID reader.
#[cfg(test)]

mod tests {

    // Import conversion helpers from the parent module.
    use super::*;

    #[test]
    /// Ensure conversion preserves all bytes and their order.

    fn keeps_full_uid() {

        // Consecutive bytes make swapped or dropped values easy to recognise.
        assert_eq!(
            to_registers([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
            [1, 0x203, 0x405, 0x607, 0x809, 0xa0b]
        );
    }
}
