#[derive(Clone, Copy, Debug)]

pub struct HealthGate {
    last_main: u32,
    last_isr: u32,
}

impl HealthGate {
    pub const fn new() -> Self {
        Self {
            last_main: 0,
            last_isr: 0,
        }
    }

    pub fn main_progress(&mut self, n: u32) {
        self.last_main = n
    }

    pub fn isr_progress(&mut self, n: u32) {
        self.last_isr = n
    }

    pub const fn may_feed(&self, now: u32, max_stall: u32, pending_work: bool) -> bool {
        now.wrapping_sub(self.last_main) <= max_stall
            && (!pending_work || now.wrapping_sub(self.last_isr) <= max_stall)
    }
}

impl Default for HealthGate {
    fn default() -> Self {
        Self::new()
    }
}
