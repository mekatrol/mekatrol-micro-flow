//! Hardware-independent Modbus RTU receive framing.
//!
//! The USART interrupt supplies bytes with monotonic microsecond timestamps.
//! A periodic timer or foreground worker calls `poll` during silence. Neither
//! operation parses Modbus commands or performs application work.
//!
//! This module deliberately does not claim to provide interrupt safety:
//! its owner must serialize ISR and foreground access using short critical
//! sections or transfer ownership through a verified interrupt-safe channel.

use crate::timer::{T1_5_US, T3_5_US};
use crate::uart::RxHandoff;

/// A timed Modbus RTU receiver with two frame buffers.
///
/// Timestamps and timing thresholds use the same unit:
/// microseconds by default, or hardware ticks in firmware.
///
/// Wrapping subtraction supports a wrapping 32-bit counter,
/// provided the receiver is polled frequently enough.
pub struct RtuReceiver {
    frames: RxHandoff,
    last_byte_us: Option<u32>,
    invalid: bool,
    rejected: u32,

    /// Maximum permitted interval within a frame.
    t1_5: u32,

    /// Minimum silence required to complete a frame.
    t3_5: u32,
}

impl RtuReceiver {
    /// Construct a receiver using microsecond timestamps.
    ///
    /// This preserves the existing interface for unit tests
    /// and hardware-independent callers.
    pub const fn new() -> Self {
        Self::new_with_thresholds(T1_5_US, T3_5_US)
    }

    /// Construct a receiver using hardware counter ticks.
    ///
    /// `ticks_per_us` must be the number of hardware counter
    /// increments occurring in one microsecond.
    ///
    /// For example, at 48 MHz:
    /// - t1.5 = T1_5_US * 48 ticks
    /// - t3.5 = T3_5_US * 48 ticks
    pub const fn new_with_tick_rate(ticks_per_us: u32) -> Self {
        assert!(ticks_per_us > 0);
        assert!(T3_5_US <= u32::MAX / ticks_per_us);

        Self::new_with_thresholds(T1_5_US * ticks_per_us, T3_5_US * ticks_per_us)
    }

    /// Construct a receiver with explicit timing thresholds.
    ///
    /// All timestamps supplied to `on_byte()` and `poll()`
    /// must use the same units as these thresholds.
    const fn new_with_thresholds(t1_5: u32, t3_5: u32) -> Self {
        Self {
            frames: RxHandoff::new(),
            last_byte_us: None,
            invalid: false,
            rejected: 0,
            t1_5,
            t3_5,
        }
    }

    /// Record one byte received at `now_us`.
    ///
    /// An intra-frame gap greater than t1.5 but shorter than t3.5
    /// invalidates the entire frame. A gap of at least t3.5 finishes
    /// the preceding frame before this byte starts the next one.
    pub fn on_byte(&mut self, byte: u8, now_us: u32) {
        if let Some(last) = self.last_byte_us {
            let elapsed = now_us.wrapping_sub(last);

            if elapsed >= self.t3_5 {
                // Sufficient silence separates two Modbus RTU frames.
                self.finish();
            } else if elapsed > self.t1_5 {
                // An intermediate gap invalidates the current frame.
                self.invalid = true;
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
            if now_us.wrapping_sub(last) >= self.t3_5 {
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

    #[test]
    fn hardware_ticks_preserve_rtu_timing() {
        // Simulate a SysTick counter running at 48 MHz.
        const TICKS_PER_US: u32 = 48;

        let mut rx = RtuReceiver::new_with_tick_rate(TICKS_PER_US);

        // Four bytes arriving 100 microseconds apart.
        rx.on_byte(0x11, 100 * TICKS_PER_US);
        rx.on_byte(0x03, 200 * TICKS_PER_US);
        rx.on_byte(0x00, 300 * TICKS_PER_US);
        rx.on_byte(0x00, 400 * TICKS_PER_US);

        // The frame must not be published before t3.5.
        rx.poll((400 + T3_5_US - 1) * TICKS_PER_US);
        assert!(!rx.frames().has_ready_frame());

        // At t3.5, the frame becomes available.
        rx.poll((400 + T3_5_US) * TICKS_PER_US);

        assert_eq!(
            rx.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }
}
