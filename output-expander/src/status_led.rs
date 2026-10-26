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
