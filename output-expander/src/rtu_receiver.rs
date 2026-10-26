//! Hardware-independent Modbus RTU receive framing.
//!
//! The USART interrupt supplies bytes with monotonic hardware timestamps.
//! A foreground worker consumes these events and calls `poll()` during
//! silence to detect completed Modbus RTU frames.
//!
//! Timestamps are expressed in microseconds by default or hardware ticks
//! when the receiver is constructed using `new_with_tick_rate()`.
//!
//! The receiver does not parse Modbus commands, validate CRCs, or perform
//! application work. It only manages frame timing and buffering.
//!
//! This module is not independently interrupt-safe. Its owner must
//! synchronize access or transfer events through an interrupt-safe queue.

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

    /// Whether incoming bytes must be discarded until an RTU t3.5
    /// silent interval restores frame synchronization.
    discarding: bool,
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

            // Start synchronized and ready to receive a new RTU frame.
            discarding: false,

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
        // While synchronization is lost, ignore bytes until a complete
        // t3.5 silent interval separates the damaged frame from a new one.
        if self.discarding {
            match self.last_byte_us {
                Some(last) if now_us.wrapping_sub(last) >= self.t3_5 => {
                    // Silence restored synchronization. Process this byte
                    // as the beginning of a new frame.
                    self.discarding = false;
                    self.last_byte_us = None;
                }
                _ => {
                    // The byte still belongs to the damaged stream.
                    self.last_byte_us = Some(now_us);
                    return;
                }
            }
        }

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

    /// Handle loss of synchronization in the UART receive stream.
    ///
    /// This occurs when interrupt queue overflow causes received bytes
    /// to be lost. The incomplete frame is discarded, and subsequent
    /// bytes are ignored until a complete Modbus RTU t3.5 silent
    /// interval has elapsed.
    ///
    /// `now_ticks` is the current free-running SysTick counter value.
    pub fn on_stream_loss(&mut self, now_ticks: u32) {
        // Discard the incomplete frame without touching a completed
        // frame that may already be waiting for processing.
        self.frames.discard_receiving();

        // Reset the previous frame's validation state.
        self.invalid = false;

        // Ignore further bytes until a valid frame boundary is observed.
        self.discarding = true;

        // Start measuring the recovery silence interval.
        self.last_byte_us = Some(now_ticks);

        // Record the rejected frame for diagnostics.
        self.rejected = self.rejected.saturating_add(1);
    }

    /// Invalidate all pending receive data after a transport-level failure.
    ///
    /// Unlike ordinary stream-loss recovery, this also discards a
    /// completed frame that has not yet been processed by the server.
    ///
    /// Call this when the application cannot establish that a pending
    /// frame was received before the transport failure.
    pub fn invalidate_pending_frames(&mut self, now_ticks: u32) {
        // Enter synchronization recovery and discard the partial frame.
        self.on_stream_loss(now_ticks);

        // Release any completed request that has not yet been processed.
        self.frames.release_ready();
    }

    /// Handle a UART parity, framing, noise, or overrun error.
    ///
    /// A corrupted UART byte invalidates the current Modbus RTU frame.
    /// The receiver must discard subsequent bytes until a complete
    /// t3.5 silent interval restores synchronization.
    ///
    /// `now_ticks` is the timestamp associated with the UART error.
    pub fn on_uart_error(&mut self, now_ticks: u32) {
        // Use the same synchronization recovery as a lost UART event.
        self.on_stream_loss(now_ticks);
    }

    /// Publish a frame only after at least t3.5 of silence.
    ///
    /// This must be driven by a timer even when no further UART bytes arrive.
    pub fn poll(&mut self, now_us: u32) {
        // Recovery ends only after a full t3.5 interval without bytes.
        if self.discarding {
            if let Some(last) = self.last_byte_us {
                if now_us.wrapping_sub(last) >= self.t3_5 {
                    self.discarding = false;
                    self.last_byte_us = None;
                }
            }

            return;
        }

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

    /// Copy a completed RTU request into an application-owned buffer.
    ///
    /// Returns the request length when a completed frame is available.
    /// Returns `None` if no frame is ready or the destination is too small.
    ///
    /// The ready frame is released only after a successful copy, allowing
    /// the receiver to reuse its buffer for subsequent requests.
    ///
    /// This method does not validate the Modbus CRC or execute commands.
    pub fn take_ready_frame(&mut self, destination: &mut [u8]) -> Option<usize> {
        // Access the completed frame without modifying its contents.
        let frame = self.frames.ready_frame()?;

        // Never truncate a Modbus request.
        if frame.len() > destination.len() {
            return None;
        }

        // Copy the request into storage owned by the application.
        let length = frame.len();
        destination[..length].copy_from_slice(frame);

        // Release the completed frame after the copy succeeds.
        self.frames.release_ready();

        Some(length)
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

        // Begin receiving a frame.
        rx.on_byte(1, 0);

        // A UART error invalidates the incomplete frame.
        rx.on_uart_error(100);

        // The frame must not become available during recovery.
        rx.poll(100 + T3_5_US);
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

    #[test]
    fn stream_loss_discards_until_silence() {
        let mut rx = RtuReceiver::new_with_tick_rate(1);

        rx.on_byte(0x11, 0);
        rx.on_byte(0x03, 100);

        // The receive stream becomes unreliable.
        rx.on_stream_loss(200);

        // Remaining bytes of the damaged request must be ignored.
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);

        rx.poll(400 + T3_5_US - 1);
        assert!(rx.frames().ready_frame().is_none());

        // A complete silent interval restores synchronization.
        rx.poll(400 + T3_5_US);

        // A new, complete request is accepted.
        let start = 400 + T3_5_US + 100;

        rx.on_byte(0x11, start);
        rx.on_byte(0x03, start + 100);
        rx.on_byte(0x00, start + 200);
        rx.on_byte(0x00, start + 300);

        rx.poll(start + 300 + T3_5_US);

        assert_eq!(
            rx.frames().ready_frame(),
            Some(&[0x11, 0x03, 0x00, 0x00][..])
        );
    }

    #[test]
    fn transport_failure_invalidates_pending_frame() {
        let mut rx = RtuReceiver::new();

        // Receive a complete four-byte request.
        rx.on_byte(0x11, 100);
        rx.on_byte(0x03, 200);
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);

        // Publish the request after the required silent interval.
        rx.poll(400 + T3_5_US);

        assert!(rx.frames().has_ready_frame());

        // Simulate a transport failure before server processing.
        rx.invalidate_pending_frames(400 + T3_5_US + 100);

        // The application must not process the pending request.
        assert!(!rx.frames().has_ready_frame());

        // Recovery must not publish the damaged stream.
        rx.poll(400 + 2 * T3_5_US + 100);
        assert!(!rx.frames().has_ready_frame());
    }

    #[test]
    fn completed_frame_can_be_copied_and_released() {
        let mut rx = RtuReceiver::new();

        // Receive an illustrative four-byte RTU frame.
        // CRC validation belongs to the Modbus server.
        rx.on_byte(0x11, 100);
        rx.on_byte(0x03, 200);
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);

        // Publish the frame after the required silent interval.
        rx.poll(400 + T3_5_US);

        // Copy the completed request into application-owned storage.
        let mut request = [0u8; 18];
        let length = rx.take_ready_frame(&mut request);

        assert_eq!(length, Some(4));
        assert_eq!(&request[..4], &[0x11, 0x03, 0x00, 0x00]);

        // The receiver must release its completed frame.
        assert!(!rx.frames().has_ready_frame());

        // Repeated reads must not return the same request.
        assert_eq!(rx.take_ready_frame(&mut request), None);
    }

    #[test]
    fn insufficient_destination_preserves_ready_frame() {
        let mut rx = RtuReceiver::new();

        // Receive and complete a four-byte frame.
        rx.on_byte(0x11, 100);
        rx.on_byte(0x03, 200);
        rx.on_byte(0x00, 300);
        rx.on_byte(0x00, 400);
        rx.poll(400 + T3_5_US);

        // A short destination must not receive a truncated request.
        let mut short_buffer = [0u8; 2];

        assert_eq!(rx.take_ready_frame(&mut short_buffer), None);

        // The complete frame remains available for a later copy.
        assert!(rx.frames().has_ready_frame());
    }
}
