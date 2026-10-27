//! Physical GPIO driver for the eight expansion-board outputs.

use ch32_hal::gpio::Output;

use crate::board::{OUTPUT_COUNT, output_pin_is_high};

/// Owns the output pins in logical channel order.
///
/// Index zero is channel 1/coil 0. The caller constructs every pin in its
/// safe OFF level before transferring ownership here.
pub struct OutputGpios<'d> {
    pins: [Output<'d>; OUTPUT_COUNT],
}

impl<'d> OutputGpios<'d> {
    pub fn new(pins: [Output<'d>; OUTPUT_COUNT]) -> Self {
        Self { pins }
    }

    /// Apply all eight logical states from one coherent bitmask.
    pub fn apply_mask(&mut self, mask: u8) {
        for (channel, pin) in self.pins.iter_mut().enumerate() {
            if output_pin_is_high(mask, channel).unwrap_or(true) {
                pin.set_high();
            } else {
                pin.set_low();
            }
        }
    }
}
