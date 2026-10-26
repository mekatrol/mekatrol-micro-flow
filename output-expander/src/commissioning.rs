use crate::uid::UID_BYTES;

pub const FUNCTION: u8 = 0x41;

pub const ASSIGN_UID: u8 = 0x03;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub struct Assignment {
    pub uid: [u8; UID_BYTES],
    pub address: u8,
}

pub fn parse_assignment(payload: &[u8]) -> Option<Assignment> {

    if payload.len() != UID_BYTES + 2
        || payload[0] != ASSIGN_UID
        || !(1..=247).contains(&payload[UID_BYTES + 1])
    {

        return None;
    }

    let mut uid = [0; UID_BYTES];

    uid.copy_from_slice(&payload[1..=UID_BYTES]);

    Some(Assignment {
        uid,
        address: payload[UID_BYTES + 1],
    })
}

pub fn applies(assignment: &Assignment, local: &[u8; UID_BYTES], configured: bool) -> bool {

    !configured && assignment.uid == *local
}
