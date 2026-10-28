/// The CH32 chip exposes a 96-bit, or 12-byte, unique identifier.

pub const UID_BYTES: usize = 12;

// The hardware UID peripheral exists only in the actual bare-metal target.
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
/// Read the immutable silicon UID from the microcontroller.

pub fn read() -> [u8; UID_BYTES] {
    // The HAL hides the chip-specific memory addresses and volatile access.
    ch32_hal::signature::unique_id()
}
