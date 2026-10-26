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
}
