/// Start, eight data, parity, and stop bits make an 8E1 character 11 bits.

pub const BITS_PER_CHAR_8E1: u32 = 11;

/// Modbus's 1.5-character gap threshold at the configured line speed.

pub const T1_5_US: u32 = 860;

/// Modbus's 3.5-character end-of-frame threshold, in microseconds.

pub const T3_5_US: u32 = 2006;

/// Calculate one complete 8E1 character time for an arbitrary baud rate.

pub const fn character_time_us(baud: u32) -> u32 {

    // Multiply bits by microseconds per second, then round upward so timing
    // never ends earlier than a complete character on the wire.
    (BITS_PER_CHAR_8E1 * 1_000_000).div_ceil(baud)
}

// These traits make timing classifications easy to copy and test.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Meaning of a silent interval observed between received serial bytes.

pub enum Gap {
    /// Short enough that the next byte remains part of the current frame.
    WithinFrame,
    /// Too long within a frame but too short to delimit a new valid frame.
    InvalidFrame,
    /// Long enough to finish the old frame and permit a new one.
    FrameComplete,
}

/// Interpret a measured quiet interval using Modbus RTU timing rules.

pub const fn classify_gap(us: u32) -> Gap {

    // Check the largest threshold first because it also exceeds 1.5 chars.
    if us >= T3_5_US {

        Gap::FrameComplete
    } else if us > T1_5_US {

        // A middle-sized gap corrupts the current frame under the RTU rules.
        Gap::InvalidFrame
    } else {

        // A short pause is ordinary spacing between bytes in the same frame.
        Gap::WithinFrame
    }
}

// Compile these checks only when running tests on a host.
#[cfg(test)]

mod tests {

    // Import the timing API from the parent module.
    use super::*;

    #[test]
    /// Lock down the configured timing values and boundary behaviour.

    fn timings() {

        // 11 bits at 19,200 baud rounds upward to 573 microseconds.
        assert_eq!(character_time_us(19_200), 573);

        // Equality at the 3.5-character threshold completes a frame.
        assert_eq!(classify_gap(2006), Gap::FrameComplete);
    }
}
