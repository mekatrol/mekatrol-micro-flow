#![no_std]

/// Number of independently controlled outputs on the board.
pub const OUTPUT_COUNT: usize = 8;

/// Logical state shared by the eight-output hardware variants.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OutputState(u8);

impl OutputState {
    pub const ALL_OFF: Self = Self(0);

    pub const fn from_bits(bits: u8) -> Self {
        Self(bits)
    }

    pub const fn bits(self) -> u8 {
        self.0
    }

    pub const fn is_on(self, output: usize) -> bool {
        output < OUTPUT_COUNT && self.0 & (1 << output) != 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exposes_eight_output_bits() {
        let state = OutputState::from_bits(0b1000_0001);

        assert!(state.is_on(0));
        assert!(state.is_on(7));
        assert!(!state.is_on(1));
        assert!(!state.is_on(OUTPUT_COUNT));
    }
}
