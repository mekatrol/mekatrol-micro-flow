// Derive standard traits so this tiny state value is copyable and testable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// State of all eight output channels packed into one byte.

pub struct Outputs {
    // Bit N represents channel N; one means on and zero means off.
    mask: u8,
}

impl Outputs {
    /// Construct the fail-safe startup state with every output off.

    pub const fn new_off() -> Self {

        // A zero byte has no set bits, so no channel is energised.
        Self { mask: 0 }
    }

    /// Return the complete packed state for reporting or hardware updates.

    pub const fn mask(&self) -> u8 {

        // `u8` is copied, so reading it does not consume or change `self`.
        self.mask
    }

    /// Replace all eight channel states in a single operation.

    pub fn set_mask(&mut self, mask: u8) {

        // Every bit is meaningful, so the byte needs no further validation.
        self.mask = mask;
    }

    /// Set one channel, rejecting numbers outside zero through seven.

    pub fn set_channel(&mut self, channel: u8, on: bool) -> Result<(), ()> {

        // An eight-bit value has only eight valid bit positions.
        if channel >= 8 {

            // Unit `()` is sufficient because this function has one error kind.
            return Err(());
        }

        // Shift one into the selected channel's bit position.
        let bit = 1u8 << channel;

        // OR sets only this bit; AND with its inverse clears only this bit.
        if on {

            self.mask |= bit
        } else {

            self.mask &= !bit
        }

        // The requested channel was valid and is now updated.
        Ok(())
    }

    /// Read a channel, returning `None` for an invalid channel number.

    pub const fn channel(&self, channel: u8) -> Option<bool> {

        // Validate before shifting so the shift amount is always safe.
        if channel < 8 {

            // A non-zero bitwise AND means the chosen channel is on.
            Some(self.mask & (1 << channel) != 0)
        } else {

            // No stored bit corresponds to channel eight or above.
            None
        }
    }
}

// Rust's conventional default is deliberately the safe all-off state.
impl Default for Outputs {
    fn default() -> Self {

        // Reuse the constructor so both default paths remain identical.
        Self::new_off()
    }
}

// Test-only code is omitted from the embedded firmware binary.
#[cfg(test)]

mod tests {

    // Import the parent module's names for concise tests.
    use super::*;

    #[test]
    /// Confirm that channel zero maps to the least-significant bit.

    fn channel_mapping_is_lsb_first() {

        // Begin with a known empty mask.
        let mut o = Outputs::new_off();

        // Turn on each successive channel and inspect the accumulated bits.
        for n in 0..8 {

            // Unwrap is appropriate because every loop value is known valid.
            o.set_channel(n, true).unwrap();

            // The lowest N+1 bits should now all be set.
            assert_eq!(o.mask(), ((1u16 << (n + 1)) - 1) as u8);
        }

        // Eight is just beyond the valid channel range and must fail.
        assert!(o.set_channel(8, true).is_err());
    }
}
