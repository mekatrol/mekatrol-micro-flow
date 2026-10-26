//! Hardware-independent Modbus RTU receive framing.
//!
//! The USART interrupt supplies bytes with monotonic microsecond timestamps.
//! A periodic timer or foreground worker calls `poll` during silence. Neither
//! operation parses Modbus commands or performs application work.
//!
//! This module deliberately does not claim to provide interrupt safety:
//! its owner must serialize ISR and foreground access using short critical
//! sections or transfer ownership through a verified interrupt-safe channel.

use crate::timer::{Gap, T3_5_US, classify_gap};
use crate::uart::RxHandoff;

/// A timed receiver with one accumulating and one completed frame.
///
/// Timestamp arithmetic wraps safely when the timer is a wrapping `u32`
/// microsecond counter. The caller must invoke `poll` frequently enough to
/// detect a 3.5-character gap before the next frame starts.
pub struct RtuReceiver {
    frames: RxHandoff,
    last_byte_us: Option<u32>,
    invalid: bool,
    rejected: u32,
}

impl RtuReceiver {
    /// Construct a receiver without allocating memory.
    pub const fn new() -> Self {
        Self {
            frames: RxHandoff::new(),
            last_byte_us: None,
            invalid: false,
            rejected: 0,
        }
    }

    /// Record one byte received at `now_us`.
    ///
    /// An intra-frame gap greater than t1.5 but shorter than t3.5
    /// invalidates the entire frame. A gap of at least t3.5 finishes
    /// the preceding frame before this byte starts the next one.
    pub fn on_byte(&mut self, byte: u8, now_us: u32) {
        if let Some(last) = self.last_byte_us {
            match classify_gap(now_us.wrapping_sub(last)) {
                Gap::WithinFrame => {}
                Gap::InvalidFrame => self.invalid = true,
                Gap::FrameComplete => self.finish(),
            }
        }
        if !self.frames.push_byte(byte) {
            self.invalid = true;
        }
        self.last_byte_us = Some(now_us);
    }

    /// Discard the current frame after a parity, framing, or overrun error.
    /// The remaining bytes are ignored until the next frame boundary.
    pub fn on_uart_error(&mut self) {
        self.invalid = true;
    }

    /// Publish a frame only after at least t3.5 of silence.
    ///
    /// This must be driven by a timer even when no further UART bytes arrive.
    pub fn poll(&mut self, now_us: u32) {
        if let Some(last) = self.last_byte_us {
            if now_us.wrapping_sub(last) >= T3_5_US {
                self.finish();
            }
        }
    }

    /// Access the handoff only while its owning context is synchronized.
    pub fn frames(&mut self) -> &mut RxHandoff {
        &mut self.frames
    }

    /// Number of frames rejected due to UART errors or timing violations.
    pub fn rejected(&self) -> u32 {
        self.rejected
    }

    fn finish(&mut self) {
        // A Modbus RTU request requires at least four bytes:
        // slave address, function code, and two CRC bytes.
        //
        // WHY:
        // A timing gap can terminate an incomplete transmission.
        // Such fragments must not be handed to the Modbus server.
        let too_short = self.frames.receiving_bytes().len() < 4;

        if self.invalid || too_short {
            // Discard bytes from the interrupted or invalid frame.
            //
            // This also prepares the receiving buffer for the
            // next frame without allocating memory.
            self.frames.discard_receiving();

            // Count the rejected frame for diagnostics.
            self.rejected = self.rejected.saturating_add(1);
        } else {
            // Publish a completed frame.
            //
            // RxHandoff protects a previously completed request
            // from being overwritten by a new request.
            //
            // CRC and Modbus command validation remain the
            // responsibility of the Modbus server.
            self.frames.complete_frame();
        }

        // Reset timing and validity state so the next received
        // byte begins a new independent RTU frame.
        self.invalid = false;
        self.last_byte_us = None;
    }
}

impl Default for RtuReceiver {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // The t1.5 constant is needed only by the unit tests.
    // Keeping it here avoids an unused-import warning
    // when compiling the firmware for the CH32V003.
    use crate::timer::T1_5_US;

    #[test]
    fn frame_published_only_after_silence() {
        let mut rx = RtuReceiver::new();

        // A Modbus RTU frame must contain at least four bytes:
        // address, function code, and two CRC bytes.
        //
        // These bytes are used to test frame timing and handoff.
        // CRC validation is handled separately by the Modbus server.
        rx.on_byte(0x11, 100);
        rx.on_byte(0x03, 200);
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);

        // Before the required t3.5 silence interval expires,
        // the frame must remain in the receiving buffer.
        rx.poll(400 + T3_5_US - 1);
        assert!(!rx.frames().has_ready_frame());

        // Once t3.5 silence has elapsed, the completed frame
        // becomes available to the application.
        rx.poll(400 + T3_5_US);

        assert_eq!(
            rx.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }

    #[test]
    fn middle_gap_invalidates_frame() {
        let mut rx = RtuReceiver::new();
        rx.on_byte(0x11, 0);
        rx.on_byte(0x03, T1_5_US + 1);
        rx.poll(T1_5_US + 1 + T3_5_US);
        assert!(!rx.frames().has_ready_frame());
        assert_eq!(rx.rejected(), 1);
    }

    #[test]
    fn next_frame_does_not_replace_pending_frame() {
        let mut rx = RtuReceiver::new();

        // Receive and publish the first four-byte frame.
        //
        // The bytes are illustrative; CRC validation is
        // performed by the Modbus server, not this receiver.
        rx.on_byte(0x01, 100);
        rx.on_byte(0x03, 200);
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);

        // Complete the first frame after t3.5 silence.
        rx.poll(400 + T3_5_US);

        assert_eq!(
            rx.frames().ready_frame(),
            Some(&[0x01, 0x03, 0x00, 0x00][..])
        );

        // Receive a second frame while the first is still
        // waiting to be processed by the main loop.
        let start = 400 + T3_5_US + 100;

        rx.on_byte(0x02, start);
        rx.on_byte(0x03, start + 100);
        rx.on_byte(0x00, start + 200);
        rx.on_byte(0x00, start + 300);

        // The second frame completes, but the ready buffer
        // is still occupied by the first frame.
        rx.poll(start + 300 + T3_5_US);

        // The first frame must remain unchanged.
        //
        // WHY:
        // A newly received Modbus command must never silently
        // overwrite a previously completed, unprocessed command.
        assert_eq!(
            rx.frames().ready_frame(),
            Some(&[0x01, 0x03, 0x00, 0x00][..])
        );

        // Verify that the second frame was discarded.
        assert_eq!(rx.frames().dropped_frames(), 1);
    }

    #[test]
    fn uart_error_discards_frame() {
        let mut rx = RtuReceiver::new();
        rx.on_byte(1, 0);
        rx.on_uart_error();
        rx.poll(T3_5_US);
        assert!(!rx.frames().has_ready_frame());
    }
}
