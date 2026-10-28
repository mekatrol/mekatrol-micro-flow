/// Nominal independent-watchdog period.
pub const TIMEOUT_MS: u32 = 2_000;

#[cfg(all(target_arch = "riscv32", target_os = "none"))]
/// CH32V003 independent watchdog, configured for a nominal two-second period.
pub struct IndependentWatchdog;

#[cfg(all(target_arch = "riscv32", target_os = "none"))]
impl IndependentWatchdog {
    /// Configure and start the watchdog. Once started it can only be stopped by
    /// a device reset.
    pub fn start() -> Self {
        // The CH32V003 IWDG runs from the nominal 128 kHz LSI. A /64
        // prescaler and reload value 3999 produce a nominal 2.0 s period.
        // The pinned PAC omits this peripheral for the CH32V003, so keep the
        // four documented IWDG register accesses contained in this wrapper.
        unsafe {
            write_register(CTLR, 0x5555);
            write_register(PSCR, 4);
            write_register(RLDR, 3_999);
            // WCH explicitly permits continuing without polling PVU/RVU. In
            // particular, polling here before starting the IWDG can stall
            // startup because the LSI clock is not running yet.
            write_register(CTLR, 0xAAAA);
            write_register(CTLR, 0xCCCC);
        }

        Self
    }

    /// Reload the watchdog counter from the foreground main loop.
    pub fn feed(&mut self) {
        unsafe { write_register(CTLR, 0xAAAA) };
    }
}

#[cfg(all(target_arch = "riscv32", target_os = "none"))]
const IWDG_BASE: usize = 0x4000_3000;
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
const CTLR: usize = IWDG_BASE;
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
const PSCR: usize = IWDG_BASE + 0x04;
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
const RLDR: usize = IWDG_BASE + 0x08;
#[cfg(all(target_arch = "riscv32", target_os = "none"))]
unsafe fn write_register(address: usize, value: u32) {
    unsafe { core::ptr::write_volatile(address as *mut u32, value) };
}

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
