// Derived traits support copying, debugging, and equality tests.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Operating conditions represented by the RGB status LED.

pub enum Status {
    /// The device has no assigned Modbus address yet.
    Unconfigured,
    /// The device is operating normally.
    Healthy,
    /// A command was just handled, giving visible activity feedback.
    Command,
    /// No valid communication arrived before the configured deadline.
    CommsTimeout,
    /// Persistent configuration storage failed validation or writing.
    FlashFault,
}

// This tuple struct stores channels compactly in red, green, blue order.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Red, green, and blue intensity values for the physical LED.

pub struct Rgb(pub u8, pub u8, pub u8);

pub const OFF: Rgb = Rgb(0, 0, 0);

/// Select the visible LED colour for the current millisecond timestamp.
///
/// Priority is command activity, error, then the healthy heartbeat. This lets
/// a received command flash blue even while an error is present, after which
/// the error pattern resumes. Errors blink red for 300 ms on and 300 ms off.
/// A command is blue for 100 ms. In the normal state green is on for the first
/// 20 ms of every second.
pub const fn pattern_color(
    now_ms: u32,
    brightness: u8,
    error_present: bool,
    command_elapsed_ms: Option<u32>,
) -> Rgb {
    if matches!(command_elapsed_ms, Some(elapsed) if elapsed < 100) {
        Rgb(0, 0, brightness)
    } else if error_present {
        if (now_ms / 300) & 1 == 0 {
            Rgb(brightness, 0, 0)
        } else {
            OFF
        }
    } else if now_ms % 1_000 < 20 {
        Rgb(0, brightness, 0)
    } else {
        OFF
    }
}

/// Encode an RGB colour in the GRB byte order expected by WS2812 LEDs.
pub const fn ws2812_word(Rgb(red, green, blue): Rgb) -> u32 {
    ((green as u32) << 16) | ((red as u32) << 8) | blue as u32
}

/// Map a status to a recognisable colour at a caller-selected brightness.

pub const fn color(status: Status, brightness: u8) -> Rgb {
    // An exhaustive match ensures any future status must choose a colour.
    match status {
        // Amber signals that commissioning is needed but hardware is alive.
        Status::Unconfigured => Rgb(brightness, brightness / 3, 0),
        // Green communicates normal operation.
        Status::Healthy => Rgb(0, brightness, 0),
        // Blue is a short-lived command activity indication.
        Status::Command => Rgb(0, 0, brightness),
        // Red groups both conditions that require operator attention.
        Status::CommsTimeout | Status::FlashFault => Rgb(brightness, 0, 0),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encodes_ws2812_bytes_in_grb_order() {
        assert_eq!(ws2812_word(Rgb(0x12, 0x34, 0x56)), 0x34_12_56);
    }

    #[test]
    fn command_activity_overrides_error_pattern_temporarily() {
        let brightness = 32;

        assert_eq!(pattern_color(0, brightness, false, None), Rgb(0, 32, 0));
        assert_eq!(pattern_color(20, brightness, false, None), OFF);
        assert_eq!(
            pattern_color(500, brightness, false, Some(10)),
            Rgb(0, 0, 32)
        );
        assert_eq!(pattern_color(0, brightness, true, Some(10)), Rgb(0, 0, 32));
        assert_eq!(pattern_color(300, brightness, true, Some(100)), OFF);
        assert_eq!(pattern_color(600, brightness, true, None), Rgb(32, 0, 0));
    }
}
