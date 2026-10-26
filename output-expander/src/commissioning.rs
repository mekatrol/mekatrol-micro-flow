// Use the shared UID length so network messages always match the silicon ID.
use crate::uid::UID_BYTES;

/// Vendor-specific Modbus function code reserved for commissioning.

pub const FUNCTION: u8 = 0x41;

/// Sub-command meaning "assign an address to this exact device UID".

pub const ASSIGN_UID: u8 = 0x03;

// These traits make assignments cheap to copy, printable, and comparable.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// A validated request to give one physical device a Modbus address.

pub struct Assignment {
    /// Full silicon ID, used to select one device on a shared bus.
    pub uid: [u8; UID_BYTES],
    /// Valid Modbus unicast address in the range 1 through 247.
    pub address: u8,
}

/// Validate and decode a commissioning payload.
///
/// `Some` contains a safe assignment; `None` tells the caller to ignore a
/// malformed request without needing exceptions or heap allocation.

pub fn parse_assignment(payload: &[u8]) -> Option<Assignment> {

    // Require the exact layout, expected command, and legal address. The `||`
    // checks short-circuit, so bad lengths are rejected before risky indexing.
    if payload.len() != UID_BYTES + 2
        || payload[0] != ASSIGN_UID
        || !(1..=247).contains(&payload[UID_BYTES + 1])
    {

        // Stop before copying or using any invalid network data.
        return None;
    }

    // Create an owned fixed-size destination for the UID bytes.
    let mut uid = [0; UID_BYTES];

    // Skip the command byte and copy exactly the UID portion.
    uid.copy_from_slice(&payload[1..=UID_BYTES]);

    // Build the result; the final payload byte is the new address.
    Some(Assignment {
        uid,
        address: payload[UID_BYTES + 1],
    })
}

/// Return whether the local device should accept this assignment.
///
/// A configured device must not be moved accidentally, and an unconfigured
/// device accepts only a request containing its exact silicon UID.

pub fn applies(assignment: &Assignment, local: &[u8; UID_BYTES], configured: bool) -> bool {

    // Dereference `local` to compare the array contents, not the references.
    !configured && assignment.uid == *local
}
