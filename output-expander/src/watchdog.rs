// The health gate is a small copyable snapshot useful in diagnostics.
#[derive(Clone, Copy, Debug)]
/// Tracks progress in both normal code and interrupt-driven work.

pub struct HealthGate {
    // Tick value at which the main loop last proved it was progressing.
    last_main: u32,
    // Tick value at which the interrupt handler last made progress.
    last_isr: u32,
}

impl HealthGate {
    /// Start with both progress timestamps at tick zero.

    pub const fn new() -> Self {

        // Explicit field names make the two monitored execution paths clear.
        Self {
            last_main: 0,
            last_isr: 0,
        }
    }

    /// Record the main loop's latest progress tick.

    pub fn main_progress(&mut self, n: u32) {

        // Mutation requires `&mut self`, preventing concurrent unsafe writes.
        self.last_main = n
    }

    /// Record the interrupt-side worker's latest progress tick.

    pub fn isr_progress(&mut self, n: u32) {

        // Store the caller's monotonic wrapping counter value.
        self.last_isr = n
    }

    /// Allow a watchdog feed only while every required worker is responsive.

    pub const fn may_feed(&self, now: u32, max_stall: u32, pending_work: bool) -> bool {

        // `wrapping_sub` handles a u32 tick counter rolling through zero. The
        // interrupt timestamp matters only when interrupt work is pending.
        now.wrapping_sub(self.last_main) <= max_stall
            && (!pending_work || now.wrapping_sub(self.last_isr) <= max_stall)
    }
}

// A default gate has the same well-defined initial state as `new`.
impl Default for HealthGate {
    fn default() -> Self {

        // Delegate so future initialisation changes live in one place.
        Self::new()
    }
}
