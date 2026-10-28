//! Command execution independent of the COBS transport.
//!
//! Separating command semantics from framing keeps output changes impossible
//! until the protocol layer has validated COBS, length, version, type, and CRC.
//! It also makes command behavior testable without UART or GPIO hardware.

use crate::config::Config;
use crate::outputs::Outputs;
use crate::protocol::{Packet, TYPE_ERROR, TYPE_REQUEST, TYPE_RESPONSE};
use crate::uid::UID_BYTES;

/// Address zero selects the limited set of commands allowed to be broadcast.
pub const BROADCAST: u8 = 0;
/// Reserved address representing a device without a commissioned bus address.
pub const UNCOMMISSIONED: u8 = 0xff;
/// A validated, commissioned address is present in configuration.
pub const STATUS_CONFIGURED: u16 = 1 << 0;
/// The configured controller heartbeat interval has expired.
pub const STATUS_COMMS_TIMEOUT: u16 = 1 << 1;
/// At least one UART, framing, or CRC error has been observed.
pub const STATUS_UART_ERROR: u16 = 1 << 2;
/// Persistent configuration could not be read, committed, or verified.
pub const STATUS_CONFIG_FAULT: u16 = 1 << 3;

// Error values are fixed by protocol version one and are kept private because
// callers receive them only inside an encoded error response.
const UNKNOWN_COMMAND: u8 = 1;
const BAD_LENGTH: u8 = 2;
const BAD_VALUE: u8 = 3;

/// Protocol-visible state owned by the foreground command processor.
pub struct Dispatcher {
    /// Active configuration used by status and configuration commands.
    pub config: Config,
    /// Logical output state; hardware application remains the caller's job.
    pub outputs: Outputs,
    /// Latest sampled input bits supplied by the foreground loop.
    pub inputs: u8,
    /// Protocol status mask returned by `GET_STATUS`.
    pub status: u16,
    // Immutable silicon identity used for PING and targeted commissioning.
    uid: [u8; UID_BYTES],
}

#[derive(Clone, Copy, Default)]
/// Side effects the hardware-owning main loop must perform after dispatch.
pub struct Outcome {
    /// Unicast response to encode and transmit; broadcasts always leave this empty.
    pub response: Option<Packet>,
    /// Logical outputs changed and must be copied to physical GPIOs.
    pub outputs_changed: bool,
    /// Configuration changed and requires verified persistent storage.
    pub config_changed: bool,
    /// RESET was acknowledged, so feeding must stop only after transmission.
    pub stop_watchdog_feed: bool,
    /// This request refreshes the controller communication timeout.
    pub heartbeat: bool,
}

impl Dispatcher {
    /// Build command state from validated configuration and the hardware UID.
    pub fn new(config: Config, uid: [u8; UID_BYTES]) -> Self {
        // Construct the safe all-off state first, then apply the configured
        // startup mask. The caller similarly initializes GPIOs OFF before it
        // copies this logical state to the pins.
        let mut outputs = Outputs::new_off();
        outputs.set_mask(config.startup_outputs);

        // Report commissioning only for an address legal for unicast traffic.
        let status = if config.is_configured() {
            STATUS_CONFIGURED
        } else {
            0
        };
        Self {
            config,
            outputs,
            inputs: 0,
            status,
            uid,
        }
    }

    /// Execute one fully decoded packet and describe required hardware effects.
    pub fn handle(&mut self, request: Packet) -> Outcome {
        // Devices must never treat responses or errors observed on the shared
        // bus as commands, even if all other fields happen to be valid.
        if request.kind != TYPE_REQUEST {
            return Outcome::default();
        }
        let broadcast = request.destination == BROADCAST;

        // Address 0xFF is an internal uncommissioned state, never a usable
        // unicast destination. A factory-fresh device accepts only the
        // UID-targeted broadcast assignment handled below.
        if !broadcast
            && self.config.slave_address == UNCOMMISSIONED
            && !(request.destination == UNCOMMISSIONED && request.command == 0x01)
        {
            return Outcome::default();
        }

        // Wrong-address packets belong to another device and are silently
        // ignored to prevent multiple nodes from driving the half-duplex bus.
        if !broadcast && request.destination != self.config.slave_address {
            return Outcome::default();
        }
        // Version one permits only UID-targeted address assignment as a
        // broadcast. In particular, broadcast output changes are forbidden.
        if broadcast && request.command != 0x30 {
            return Outcome::default();
        }

        // Responses fit in the same maximum payload as requests. Reusing one
        // bounded scratch array avoids command-specific stack allocations.
        let mut out = Outcome::default();
        let mut response_payload = [0u8; 24];
        let result: Result<usize, u8> = match request.command {
            0x01 if request.payload().is_empty() && !broadcast => {
                // PING returns the complete UID so a master can prove which
                // physical board owns an address, plus the firmware version.
                response_payload[..UID_BYTES].copy_from_slice(&self.uid);
                response_payload[12] = 0;
                response_payload[13] = 1;
                out.heartbeat = true;
                Ok(14)
            }
            0x02 if request.payload().is_empty() && !broadcast => {
                // Snapshot compact live state using the documented little-
                // endian status representation.
                response_payload[0] = self.outputs.mask();
                response_payload[1] = self.inputs;
                response_payload[2..4].copy_from_slice(&self.status.to_le_bytes());
                Ok(4)
            }
            0x10 if !broadcast => {
                if request.payload().len() == 1 {
                    // Replace all channels atomically at the logical layer so
                    // the GPIO owner can apply one coherent mask afterward.
                    self.outputs.set_mask(request.payload()[0]);
                    response_payload[0] = self.outputs.mask();
                    out.outputs_changed = true;
                    out.heartbeat = true;
                    Ok(1)
                } else {
                    Err(BAD_LENGTH)
                }
            }
            0x11 if request.payload().is_empty() && !broadcast => {
                // Return logical state rather than reading active-low pins;
                // this keeps electrical polarity independent of the protocol.
                response_payload[0] = self.outputs.mask();
                Ok(1)
            }
            0x20 if request.payload().is_empty() && !broadcast => {
                // Serialize fields explicitly to define their byte order and
                // avoid including Rust struct padding in the transmitted packet.
                response_payload[0] = self.config.slave_address;
                response_payload[1] = self.config.startup_outputs;
                response_payload[2..4].copy_from_slice(&self.config.comms_timeout_ms.to_le_bytes());
                response_payload[4] = self.config.timeout_outputs;
                response_payload[5] = self.config.brightness;
                Ok(6)
            }
            0x21 if !broadcast => {
                if request.payload().len() == 5 {
                    // Decode all fields only after exact-length validation.
                    // Persistence is performed by the caller indicated by
                    // `config_changed`, keeping storage hardware out of here.
                    self.config.startup_outputs = request.payload()[0];
                    self.config.comms_timeout_ms =
                        u16::from_le_bytes([request.payload()[1], request.payload()[2]]);
                    self.config.timeout_outputs = request.payload()[3];
                    self.config.brightness = request.payload()[4];
                    response_payload[..5].copy_from_slice(request.payload());
                    out.config_changed = true;
                    Ok(5)
                } else {
                    Err(BAD_LENGTH)
                }
            }
            0x30 if broadcast => {
                // Assignment is deliberately UID-targeted because every
                // factory device hears this broadcast on a shared bus.
                if request.payload().len() != UID_BYTES + 1 {
                    Err(BAD_LENGTH)
                } else if !(1..=0xfe).contains(&request.payload()[UID_BYTES]) {
                    Err(BAD_VALUE)
                } else if request.payload()[..UID_BYTES] == self.uid {
                    // Only the exact 96-bit UID may alter this device. The
                    // broadcast still receives no response to avoid collision.
                    self.config.slave_address = request.payload()[UID_BYTES];
                    self.status |= STATUS_CONFIGURED;
                    out.config_changed = true;
                    Ok(0)
                } else {
                    // A valid assignment for a different UID is normal shared-
                    // bus traffic, not an error on this device.
                    Ok(0)
                }
            }
            0x40 if request.payload().is_empty() && !broadcast => {
                // Main must transmit the success response before it stops the
                // watchdog feed, otherwise the master cannot know reset began.
                out.stop_watchdog_feed = true;
                Ok(0)
            }
            0x01 | 0x02 | 0x10 | 0x11 | 0x20 | 0x21 | 0x30 | 0x40 => Err(BAD_LENGTH),
            // A syntactically valid unicast with an unknown command deserves
            // an explicit error so the master can distinguish it from timeout.
            _ => Err(UNKNOWN_COMMAND),
        };

        // Protocol broadcasts never produce a reply, including invalid
        // assignment payloads, because simultaneous error replies would collide.
        if broadcast {
            return out;
        }
        // Echo address, sequence, and command so the master can reject stale or
        // unrelated replies. Only the packet type and payload reflect success.
        out.response = Some(match result {
            Ok(len) => Packet::new(
                request.destination,
                request.sequence,
                TYPE_RESPONSE,
                request.command,
                &response_payload[..len],
            )
            .unwrap(),
            Err(code) => Packet::new(
                request.destination,
                request.sequence,
                TYPE_ERROR,
                request.command,
                &[code],
            )
            .unwrap(),
        });
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn request(address: u8, command: u8, payload: &[u8]) -> Packet {
        Packet::new(address, 3, TYPE_REQUEST, command, payload).unwrap()
    }
    #[test]
    fn set_outputs_changes_all_channels() {
        let mut config = Config::default();
        config.slave_address = 42;
        let mut d = Dispatcher::new(config, [7; UID_BYTES]);
        let o = d.handle(request(d.config.slave_address, 0x10, &[0xa5]));
        assert!(o.outputs_changed);
        assert_eq!(d.outputs.mask(), 0xa5);
        assert_eq!(o.response.unwrap().payload(), &[0xa5]);
    }
    #[test]
    fn broadcast_assignment_never_replies() {
        let uid = [7; UID_BYTES];
        let mut d = Dispatcher::new(Config::default(), uid);
        let mut payload = [0; UID_BYTES + 1];
        payload[..UID_BYTES].copy_from_slice(&uid);
        payload[UID_BYTES] = 42;
        let o = d.handle(request(BROADCAST, 0x30, &payload));
        assert!(o.response.is_none());
        assert!(o.config_changed);
        assert_eq!(d.config.slave_address, 42);
    }

    #[test]
    fn uncommissioned_device_accepts_only_uid_targeted_assignment() {
        let uid = [7; UID_BYTES];
        let mut d = Dispatcher::new(Config::default(), uid);

        assert_eq!(d.config.slave_address, UNCOMMISSIONED);
        assert!(!d.config.is_configured());
        let discovery = d.handle(request(UNCOMMISSIONED, 0x01, &[]));
        assert_eq!(discovery.response.unwrap().payload()[..UID_BYTES], uid);
        assert!(
            d.handle(request(UNCOMMISSIONED, 0x11, &[]))
                .response
                .is_none()
        );

        let mut wrong_uid_payload = [0; UID_BYTES + 1];
        wrong_uid_payload[UID_BYTES] = 42;
        let outcome = d.handle(request(BROADCAST, 0x30, &wrong_uid_payload));
        assert!(!outcome.config_changed);
        assert_eq!(d.config.slave_address, UNCOMMISSIONED);

        let mut payload = [0; UID_BYTES + 1];
        payload[..UID_BYTES].copy_from_slice(&uid);
        payload[UID_BYTES] = 42;
        let outcome = d.handle(request(BROADCAST, 0x30, &payload));
        assert!(outcome.response.is_none());
        assert!(outcome.config_changed);
        assert_eq!(d.config.slave_address, 42);
        assert!(d.config.is_configured());
    }
}
