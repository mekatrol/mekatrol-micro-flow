//! Free-running SysTick clock for the CH32V003.
//!
//! SysTick is configured once and must not be reset by subsequent delays.
//! The lower 32 bits are sufficient for measuring short RTU intervals.
//!
//! All elapsed-time calculations use wrapping subtraction so they remain
//! correct when the 32-bit counter wraps.

use ch32_hal::{pac, rcc};
use pac::systick::vals::Stclk;

/// SysTick ticks per microsecond.
///
/// Initialized after the HAL configures the system clock.
pub struct Monotonic {
    ticks_per_us: u32,
}

impl Monotonic {
    /// Configure SysTick to count continuously at HCLK.
    ///
    /// Call once after `hal::init()`.
    ///
    /// Do not use `hal::delay::Delay` after calling this function:
    /// that driver resets and stops the same hardware counter.
    pub fn new() -> Self {
        let hclk = rcc::clocks().hclk.0;

        assert!(
            hclk >= 1_000_000 && hclk % 1_000_000 == 0,
            "HCLK must be an integer number of MHz"
        );

        let systick = &pac::SYSTICK;

        // Disable the timer before configuring it.
        systick.ctlr().write(|_| {});

        // Start the free-running counter at zero.
        systick.cnt().write_value(0);

        // Clear any pending compare flag.
        systick.sr().write(|w| w.set_cntif(false));

        // Run continuously at HCLK, with no reload or interrupt.
        systick.ctlr().write(|w| {
            w.set_stclk(Stclk::HCLK);
            w.set_stre(false);
            w.set_stie(false);
            w.set_ste(true);
        });

        Self {
            ticks_per_us: hclk / 1_000_000,
        }
    }

    /// Read the lower 32 bits of the hardware counter.
    #[inline]
    pub fn now_ticks(&self) -> u32 {
        pac::SYSTICK.cnt().read() as u32
    }

    /// Calculate elapsed microseconds since a previously captured tick value.
    ///
    /// Valid when the actual elapsed interval is less than one 32-bit wrap.
    #[inline]
    pub fn elapsed_us(&self, since_ticks: u32) -> u32 {
        self.now_ticks().wrapping_sub(since_ticks) / self.ticks_per_us
    }

    /// Wait without resetting SysTick.
    ///
    /// Interrupts remain enabled during the wait.
    pub fn delay_us(&self, us: u32) {
        // Split long delays to avoid overflow in the multiplication.
        let max_chunk = u32::MAX / self.ticks_per_us;

        let mut remaining = us;

        while remaining != 0 {
            let chunk = remaining.min(max_chunk);
            let target_ticks = chunk * self.ticks_per_us;
            let start = self.now_ticks();

            while self.now_ticks().wrapping_sub(start) < target_ticks {
                core::hint::spin_loop();
            }

            remaining -= chunk;
        }
    }

    /// Return the number of SysTick counter ticks per microsecond.
    ///
    /// This allows protocol timing thresholds to be expressed directly
    /// in hardware ticks, avoiding division in the USART interrupt.
    #[inline]
    pub fn ticks_per_us(&self) -> u32 {
        self.ticks_per_us
    }
}
