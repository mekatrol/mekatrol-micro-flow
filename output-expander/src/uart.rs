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

/// Maximum supported Modbus RTU request length.
///
/// WHY:
/// The current firmware supports a restricted set of Modbus requests.
/// The largest supported request is the custom UID commissioning
/// broadcast, which occupies 18 bytes including its CRC.
///
/// This limit describes the current protocol implementation, not
/// the maximum possible Modbus RTU frame size.
///
/// Later, when the UART receiver becomes interrupt-driven, this
/// constant will also help define frame validation and buffer limits.
pub const MAX_REQUEST: usize = 18;

/// Determine the expected length of a supported Modbus RTU request.
///
/// WHAT:
/// Examine the address, function code, and (where necessary)
/// the byte-count field to determine the complete request length.
///
/// WHY:
/// The UART receiver needs to know when it has accumulated enough
/// bytes to pass a request to the Modbus server.
///
/// This function does not:
/// - Read UART hardware.
/// - Validate the Modbus CRC.
/// - Execute Modbus commands.
/// - Change device outputs.
///
/// Those responsibilities belong to other parts of the firmware.
///
/// RETURNS:
/// Some(length) when the request length can be determined.
/// None when more header bytes are needed or the function is unknown.
///
/// NOTE:
/// This is temporary length-based framing. Proper Modbus RTU
/// reception must eventually use inter-character and inter-frame
/// silence detection (t1.5 and t3.5).
pub fn request_length(bytes: &[u8]) -> Option<usize> {
    // A Modbus request begins with:
    // byte 0: slave address
    // byte 1: function code
    //
    // Until both are available, we cannot identify the request.
    if bytes.len() < 2 {
        return None;
    }

    match bytes[1] {
        // These functions have fixed eight-byte RTU requests:
        //
        // 0x01 - Read Coils
        // 0x03 - Read Holding Registers
        // 0x04 - Read Input Registers
        // 0x05 - Write Single Coil
        //
        // Eight bytes includes the address, function,
        // four data bytes, and two CRC bytes.
        0x01 | 0x03 | 0x04 | 0x05 => Some(8),

        // 0x0F - Write Multiple Coils
        //
        // This request contains a variable number of data bytes.
        // Byte 6 specifies how many data bytes follow.
        0x0F => {
            // Do not access byte 6 until it has arrived.
            // This prevents an out-of-bounds array access.
            if bytes.len() < 7 {
                None
            } else {
                // Seven header bytes + payload + two CRC bytes.
                Some(9 + bytes[6] as usize)
            }
        }

        // 0x41 - Vendor-specific UID commissioning.
        //
        // The existing implementation accepts this function
        // only as a broadcast addressed to slave zero.
        //
        // The complete request occupies 18 bytes.
        0x41 if bytes[0] == 0 => Some(18),

        // Unknown functions cannot be framed using this
        // restricted length table.
        //
        // A future timing-based RTU receiver will be able
        // to delimit these frames without knowing the function.
        _ => None,
    }
}

/// Capacity of the UART receive buffer.
///
/// WHAT:
/// Reserves enough space for the largest request currently
/// supported by our Modbus server.
///
/// WHY:
/// A fixed-size buffer avoids heap allocation, which is
/// important on the memory-constrained CH32V003.
///
/// This is not yet a general-purpose 256-byte Modbus RTU buffer.
/// We can increase the capacity if the supported protocol
/// functions require larger requests.
pub const RX_CAPACITY: usize = MAX_REQUEST;

/// Receive buffer used to assemble incoming UART bytes.
///
/// WHAT:
/// Stores bytes received from USART1 until the application
/// can process them as a complete Modbus request.
///
/// WHY:
/// With interrupt-driven reception, bytes can arrive while
/// the main loop is doing other work.
///
/// The interrupt handler will eventually append bytes here,
/// while the main loop processes completed frames.
///
/// IMPORTANT:
/// This structure does not provide synchronization.
/// Sharing it between interrupt and main contexts will
/// require critical sections or another safe ownership scheme.
pub struct RxFrame {
    bytes: [u8; RX_CAPACITY],
    len: usize,
    overflowed: bool,
}

impl RxFrame {
    /// Construct an empty receive frame.
    ///
    /// This is const so the structure can eventually be
    /// initialized in static storage without a heap.
    pub const fn new() -> Self {
        Self {
            bytes: [0; RX_CAPACITY],
            len: 0,
            overflowed: false,
        }
    }

    /// Append one received byte.
    ///
    /// Returns true if the byte was stored successfully.
    /// Returns false if the buffer is already full.
    ///
    /// WHY:
    /// The future USART interrupt handler must never
    /// write beyond the end of the receive buffer.
    ///
    /// Once an overflow occurs, the frame is marked
    /// invalid until reset() is called.
    pub fn push(&mut self, byte: u8) -> bool {
        if self.overflowed || self.len >= RX_CAPACITY {
            self.overflowed = true;
            return false;
        }

        self.bytes[self.len] = byte;
        self.len += 1;

        true
    }

    /// Return the bytes received so far.
    ///
    /// The returned slice contains only valid received
    /// bytes, not unused buffer capacity.
    pub fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    /// Return the number of stored bytes.
    pub fn len(&self) -> usize {
        self.len
    }

    /// Report whether no bytes have been received.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Report whether the current frame overflowed.
    ///
    /// An overflowed frame must be discarded rather
    /// than passed to the Modbus server.
    pub fn overflowed(&self) -> bool {
        self.overflowed
    }

    /// Discard the current frame and prepare for the next.
    ///
    /// WHY:
    /// Clearing the length is sufficient because unused
    /// bytes are never exposed by as_slice().
    ///
    /// This avoids unnecessarily clearing the entire
    /// buffer on every request.
    pub fn reset(&mut self) {
        self.len = 0;
        self.overflowed = false;
    }
}

impl Default for RxFrame {
    fn default() -> Self {
        Self::new()
    }
}

/// Two-buffer receive handoff for Modbus RTU frames.
///
/// WHAT:
/// Keeps the frame currently being received separate from
/// the frame waiting for application processing.
///
/// WHY:
/// In the future interrupt-driven implementation, the UART
/// interrupt must be able to continue receiving bytes while
/// the main loop processes a completed Modbus request.
///
/// This structure owns both buffers and tracks whether a
/// completed frame is waiting.
///
/// IMPORTANT:
/// This structure does not provide synchronization by itself.
/// Access from interrupt and main contexts must be protected
/// by critical sections or an equivalent safe ownership scheme.
pub struct RxHandoff {
    receiving: RxFrame,
    ready: RxFrame,
    ready_pending: bool,
    dropped_frames: u32,
}

impl RxHandoff {
    /// Construct an empty two-buffer receiver.
    ///
    /// Const initialization allows eventual use in static
    /// storage without heap allocation.
    pub const fn new() -> Self {
        Self {
            receiving: RxFrame::new(),
            ready: RxFrame::new(),
            ready_pending: false,
            dropped_frames: 0,
        }
    }

    /// Append one byte to the currently receiving frame.
    ///
    /// This will eventually be called from the UART ISR.
    ///
    /// The method only stores a byte; it does not interpret
    /// Modbus function codes or execute protocol commands.
    pub fn push_byte(&mut self, byte: u8) -> bool {
        self.receiving.push(byte)
    }

    /// Mark the current frame complete.
    ///
    /// This will eventually be called when RTU frame-gap
    /// timing indicates the end of a request.
    ///
    /// Returns true if the frame was accepted into the
    /// ready buffer, or false if it was discarded.
    pub fn complete_frame(&mut self) -> bool {
        // An empty frame is not a Modbus request.
        if self.receiving.is_empty() {
            return false;
        }

        // Never publish a frame that exceeded capacity.
        if self.receiving.overflowed() {
            self.receiving.reset();
            self.dropped_frames = self.dropped_frames.saturating_add(1);
            return false;
        }

        // The application has not consumed the previous
        // completed frame.
        //
        // POLICY:
        // Preserve the older completed frame and discard
        // the newly received frame.
        //
        // WHY:
        // Overwriting a pending request could silently
        // replace a command before the server handles it.
        if self.ready_pending {
            self.receiving.reset();
            self.dropped_frames = self.dropped_frames.saturating_add(1);
            return false;
        }

        // Exchange the two buffers.
        //
        // The previous receiving buffer becomes ready,
        // while the old (empty) ready buffer becomes
        // available for new UART data.
        core::mem::swap(&mut self.receiving, &mut self.ready);

        // Prepare the new receiving buffer for the
        // next Modbus request.
        self.receiving.reset();

        // Publish the completed frame.
        self.ready_pending = true;

        true
    }

    /// Report whether a completed frame is available.
    pub fn has_ready_frame(&self) -> bool {
        self.ready_pending
    }

    /// Borrow the completed frame without modifying it.
    ///
    /// Returns None when no completed frame is pending.
    ///
    /// IMPORTANT:
    /// The returned reference must not be used across
    /// an interrupt-visible buffer swap unless ownership
    /// or synchronization guarantees its validity.
    pub fn ready_frame(&self) -> Option<&[u8]> {
        if self.ready_pending {
            Some(self.ready.as_slice())
        } else {
            None
        }
    }

    /// Release the completed frame after processing.
    ///
    /// This makes the ready buffer available for the
    /// next completed request.
    pub fn release_ready(&mut self) {
        self.ready.reset();
        self.ready_pending = false;
    }

    /// Number of completed frames discarded because
    /// of overflow or an occupied ready buffer.
    pub fn dropped_frames(&self) -> u32 {
        self.dropped_frames
    }
}

impl Default for RxHandoff {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod framing_tests {
    use super::*;

    /// Verify that incomplete headers are handled safely.
    ///
    /// WHY:
    /// UART bytes arrive individually, so the framing code
    /// must tolerate partially received requests.
    #[test]
    fn incomplete_header() {
        assert_eq!(request_length(&[]), None);
        assert_eq!(request_length(&[247]), None);
    }

    /// Verify the supported fixed-length function codes.
    ///
    /// WHY:
    /// Moving code between modules must not change the
    /// existing request-length behavior.
    #[test]
    fn fixed_length_requests() {
        for function in [0x01, 0x03, 0x04, 0x05] {
            assert_eq!(request_length(&[247, function]), Some(8));
        }
    }

    /// Verify variable-length multiple-coil requests.
    ///
    /// WHY:
    /// The receiver must wait until the byte-count field
    /// arrives before calculating the complete length.
    #[test]
    fn multiple_coils() {
        assert_eq!(request_length(&[247, 0x0F]), None);

        assert_eq!(request_length(&[247, 0x0F, 0, 0, 0, 8, 1]), Some(10));
    }

    /// Verify that UID commissioning is broadcast-only
    /// under the current framing rules.
    #[test]
    fn commissioning_request() {
        assert_eq!(request_length(&[0, 0x41]), Some(18));

        assert_eq!(request_length(&[247, 0x41]), None);
    }

    #[test]
    fn rx_frame_stores_bytes_and_resets() {
        let mut frame = super::RxFrame::new();

        assert!(frame.is_empty());
        assert!(frame.push(0x11));
        assert!(frame.push(0x03));
        assert_eq!(frame.as_slice(), &[0x11, 0x03]);
        assert_eq!(frame.len(), 2);

        frame.reset();

        assert!(frame.is_empty());
        assert!(!frame.overflowed());
    }

    #[test]
    fn rx_frame_rejects_overflow() {
        let mut frame = super::RxFrame::new();

        for _ in 0..super::RX_CAPACITY {
            assert!(frame.push(0xAA));
        }

        assert!(!frame.push(0xBB));
        assert!(frame.overflowed());

        // Once overflowed, the frame remains invalid.
        assert!(!frame.push(0xCC));

        frame.reset();

        assert!(frame.push(0xDD));
        assert!(!frame.overflowed());
    }

    #[test]
    fn handoff_publishes_complete_frame() {
        let mut rx = super::RxHandoff::new();

        assert!(rx.push_byte(0xF7));
        assert!(rx.push_byte(0x03));

        assert!(rx.complete_frame());
        assert!(rx.has_ready_frame());
        assert_eq!(rx.ready_frame(), Some(&[0xF7, 0x03][..]));

        rx.release_ready();

        assert!(!rx.has_ready_frame());
        assert_eq!(rx.ready_frame(), None);
    }

    #[test]
    fn handoff_preserves_pending_frame() {
        let mut rx = super::RxHandoff::new();

        assert!(rx.push_byte(0x11));
        assert!(rx.complete_frame());

        // A second frame arrives before the first is consumed.
        assert!(rx.push_byte(0x22));

        // The second frame is dropped, preserving the first.
        assert!(!rx.complete_frame());
        assert_eq!(rx.ready_frame(), Some(&[0x11][..]));
        assert_eq!(rx.dropped_frames(), 1);
    }

    #[test]
    fn handoff_recovers_after_overflow() {
        let mut rx = super::RxHandoff::new();

        for _ in 0..super::RX_CAPACITY {
            assert!(rx.push_byte(0xAA));
        }

        assert!(!rx.push_byte(0xBB));
        assert!(!rx.complete_frame());

        assert_eq!(rx.dropped_frames(), 1);
        assert!(!rx.has_ready_frame());

        // The next frame can be received normally.
        assert!(rx.push_byte(0x55));
        assert!(rx.complete_frame());
        assert_eq!(rx.ready_frame(), Some(&[0x55][..]));
    }
}
