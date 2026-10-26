pub const BITS_PER_CHAR_8E1: u32 = 11;

pub const T1_5_US: u32 = 860;

pub const T3_5_US: u32 = 2006;

pub const fn character_time_us(baud: u32) -> u32 {

    (BITS_PER_CHAR_8E1 * 1_000_000).div_ceil(baud)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub enum Gap {
    WithinFrame,
    InvalidFrame,
    FrameComplete,
}

pub const fn classify_gap(us: u32) -> Gap {

    if us >= T3_5_US {

        Gap::FrameComplete
    } else if us > T1_5_US {

        Gap::InvalidFrame
    } else {

        Gap::WithinFrame
    }
}

#[cfg(test)]

mod tests {

    use super::*;

    #[test]

    fn timings() {

        assert_eq!(character_time_us(19_200), 573);

        assert_eq!(classify_gap(2006), Gap::FrameComplete);
    }
}
