#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub struct Outputs {
    mask: u8,
}

impl Outputs {
    pub const fn new_off() -> Self {

        Self { mask: 0 }
    }

    pub const fn mask(&self) -> u8 {

        self.mask
    }

    pub fn set_mask(&mut self, mask: u8) {

        self.mask = mask;
    }

    pub fn set_channel(&mut self, channel: u8, on: bool) -> Result<(), ()> {

        if channel >= 8 {

            return Err(());
        }

        let bit = 1u8 << channel;

        if on {

            self.mask |= bit
        } else {

            self.mask &= !bit
        }

        Ok(())
    }

    pub const fn channel(&self, channel: u8) -> Option<bool> {

        if channel < 8 {

            Some(self.mask & (1 << channel) != 0)
        } else {

            None
        }
    }
}

impl Default for Outputs {
    fn default() -> Self {

        Self::new_off()
    }
}

#[cfg(test)]

mod tests {

    use super::*;

    #[test]

    fn channel_mapping_is_lsb_first() {

        let mut o = Outputs::new_off();

        for n in 0..8 {

            o.set_channel(n, true).unwrap();

            assert_eq!(o.mask(), ((1u16 << (n + 1)) - 1) as u8);
        }

        assert!(o.set_channel(8, true).is_err());
    }
}
