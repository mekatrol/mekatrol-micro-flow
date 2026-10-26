/// Largest Modbus Application Data Unit accepted without dynamic allocation.

pub const MAX_ADU: usize = 128;

// State values are simple labels that benefit from copying and comparison.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// Stages in controlling the half-duplex RS-485 transceiver.

pub enum State {
    /// Receiver enabled while bytes arrive from another bus participant.
    Receiving,
    /// Required quiet delay while changing bus direction.
    Turnaround,
    /// Driver enabled while this device sends response bytes.
    Transmitting,
    /// Waiting for the final stop bit before disabling the driver.
    WaitTc,
}

// Default zeroes all counters; the other traits aid diagnostics and tests.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
/// Counts UART and buffering faults for service diagnostics.

pub struct ErrorCounters {
    /// Characters rejected because their parity bit was wrong.
    pub parity: u16,
    /// Characters whose expected stop-bit framing was missing.
    pub framing: u16,
    /// Characters lost because software did not read the UART in time.
    pub overrun: u16,
    /// Frames that exceeded the fixed receive buffer.
    pub oversized: u16,
}

/// Fixed-capacity Modbus receive buffer with validity tracking.

pub struct RxBuffer {
    // Stack/static storage avoids relying on a heap in bare-metal firmware.
    bytes: [u8; MAX_ADU],
    // Number of initial array entries containing received data.
    len: usize,
    // Once invalid, a frame remains unusable until explicitly cleared.
    invalid: bool,
}

impl RxBuffer {
    /// Construct an empty, valid receive buffer.

    pub const fn new() -> Self {

        // Zeroed unused storage is deterministic, although `len` hides it.
        Self {
            bytes: [0; MAX_ADU],
            len: 0,
            invalid: false,
        }
    }

    /// Append one byte, invalidating and rejecting an oversized frame.

    pub fn push(&mut self, byte: u8) -> Result<(), ()> {

        // Check capacity before indexing to prevent an out-of-bounds panic.
        if self.len == MAX_ADU {

            // Remember overflow even if the caller ignores this error.
            self.invalid = true;

            // Unit is sufficient because overflow is the only error here.
            Err(())
        } else {

            // Write at the first unused position.
            self.bytes[self.len] = byte;

            // Extend the visible portion only after the write succeeds.
            self.len += 1;

            // Tell the caller that this byte was retained.
            Ok(())
        }
    }

    /// Mark the current frame unusable after a UART or timing error.

    pub fn invalidate(&mut self) {

        // Preserve buffered bytes for diagnostics but prevent frame delivery.
        self.invalid = true;
    }

    /// Borrow the complete frame only when it is valid and minimally sized.

    pub fn frame(&self) -> Option<&[u8]> {

        // A Modbus RTU frame needs address, function, and two CRC bytes.
        (!self.invalid && self.len >= 4).then_some(&self.bytes[..self.len])
    }

    /// Prepare the same storage for the next frame without reallocating it.

    pub fn clear(&mut self) {

        // Reset length rather than erasing bytes because old bytes are hidden.
        self.len = 0;

        // A new frame starts valid even if the previous one failed.
        self.invalid = false;
    }
}

// Default construction matches the explicitly named empty constructor.
impl Default for RxBuffer {
    fn default() -> Self {

        // Delegate so the initial state is maintained in one place.
        Self::new()
    }
}
