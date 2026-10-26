pub const UID_BYTES: usize = 12;

pub const UID_REGISTERS: usize = 6;

/// Big-endian words in UID byte order, preserving all 96 silicon bits.

pub fn to_registers(uid: [u8; UID_BYTES]) -> [u16; UID_REGISTERS] {

    let mut out = [0; UID_REGISTERS];

    let mut i = 0;

    while i < UID_REGISTERS {

        out[i] = u16::from_be_bytes([uid[i * 2], uid[i * 2 + 1]]);

        i += 1;
    }

    out
}

#[cfg(all(target_arch = "riscv32", target_os = "none"))]

pub fn read() -> [u8; UID_BYTES] {

    ch32_hal::signature::unique_id()
}

#[cfg(test)]

mod tests {

    use super::*;

    #[test]

    fn keeps_full_uid() {

        assert_eq!(
            to_registers([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
            [1, 0x203, 0x405, 0x607, 0x809, 0xa0b]
        );
    }
}
