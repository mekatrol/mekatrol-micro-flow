pub const MAX_ADU: usize = 128;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub enum State {
    Receiving,
    Turnaround,
    Transmitting,
    WaitTc,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]

pub struct ErrorCounters {
    pub parity: u16,
    pub framing: u16,
    pub overrun: u16,
    pub oversized: u16,
}

pub struct RxBuffer {
    bytes: [u8; MAX_ADU],
    len: usize,
    invalid: bool,
}

impl RxBuffer {
    pub const fn new() -> Self {

        Self {
            bytes: [0; MAX_ADU],
            len: 0,
            invalid: false,
        }
    }

    pub fn push(&mut self, byte: u8) -> Result<(), ()> {

        if self.len == MAX_ADU {

            self.invalid = true;

            Err(())
        } else {

            self.bytes[self.len] = byte;

            self.len += 1;

            Ok(())
        }
    }

    pub fn invalidate(&mut self) {

        self.invalid = true;
    }

    pub fn frame(&self) -> Option<&[u8]> {

        (!self.invalid && self.len >= 4).then_some(&self.bytes[..self.len])
    }

    pub fn clear(&mut self) {

        self.len = 0;

        self.invalid = false;
    }
}

impl Default for RxBuffer {
    fn default() -> Self {

        Self::new()
    }
}
