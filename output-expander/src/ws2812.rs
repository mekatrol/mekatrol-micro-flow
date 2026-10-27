//! Timing-critical driver for the single WS2812 status LED on PC1.
//!
//! Edges are timed against the free-running 48 MHz SysTick counter configured
//! by `Monotonic`, avoiding timing changes caused by compiler-generated loops.

use ch32_hal::gpio::Output;
use qingke_rt::highcode;

use crate::monotonic::current_ticks;
use crate::status_led::{Rgb, ws2812_word};

// WS2812 nominal timing at 48 ticks/us. Values stay near the centre of each
// datasheet acceptance window: T0H=0.375 us, T1H=0.750 us, period=1.25 us.
const T0H_TICKS: u32 = 18;
const T1H_TICKS: u32 = 36;
const BIT_TICKS: u32 = 60;
const RESET_TICKS: u32 = 48 * 300;

/// A single, GPIO-driven WS2812 LED.
pub struct Ws2812<'d> {
    pin: Output<'d>,
}

impl<'d> Ws2812<'d> {
    /// Take ownership of a GPIO output which is already at the idle LOW level.
    pub fn new(pin: Output<'d>) -> Self {
        Self { pin }
    }

    /// Send one RGB value to the LED.
    ///
    /// Interrupts are excluded for the approximately 30 us data waveform so
    /// ISR latency cannot alter an individual bit's high time. At 19,200 bit/s
    /// this is well below one serial character time.
    pub fn set_rgb(&mut self, rgb: Rgb) {
        let word = ws2812_word(rgb);
        critical_section::with(|_| self.write_word(word));
    }

    #[inline]
    fn wait_until(start: u32, ticks: u32) {
        while current_ticks().wrapping_sub(start) < ticks {
            core::hint::spin_loop();
        }
    }

    /// Emit 24 bits, most-significant bit first, followed by the latch LOW.
    #[highcode]
    fn write_word(&mut self, word: u32) {
        for bit in (0..24).rev() {
            let start = current_ticks();
            self.pin.set_high();
            let high_ticks = if word & (1 << bit) == 0 {
                T0H_TICKS
            } else {
                T1H_TICKS
            };
            Self::wait_until(start, high_ticks);
            self.pin.set_low();
            Self::wait_until(start, BIT_TICKS);
        }

        self.pin.set_low();
        let reset_start = current_ticks();
        Self::wait_until(reset_start, RESET_TICKS);
    }
}
