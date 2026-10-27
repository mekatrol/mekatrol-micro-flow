//! Hardware-independent Modbus RTU server for the eight-output expander.
//!
//! The caller provides complete RTU frames (including address and CRC), and
//! transmits only the returned response bytes. A `None` response means silence.
//! Frame gap detection, RS-485 direction control, GPIO and flash are handled by
//! the firmware runtime, not by this module.

use crate::commissioning;
use crate::config::Config;
use crate::modbus::{self, Exception, MAX_RESPONSE, Request};
use crate::outputs::Outputs;
use crate::registers::RegisterBank;
use crate::registers::STATUS_CONFIGURED;
use crate::uid::UID_BYTES;
use crate::watchdog;

/// A processed request and any resulting configuration change.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]

pub struct Outcome {
    /// Length of the RTU reply in `response`, or zero for no reply.
    pub response_len: usize,
    /// The caller must persist `bank.config` if this is true.
    pub config_changed: bool,
    /// The caller must apply `bank.outputs.mask()` to GPIO if this is true.
    pub outputs_changed: bool,
    /// Stop feeding the hardware watchdog after transmitting this response.
    pub stop_watchdog_feed: bool,
}

impl Outcome {
    const fn silent() -> Self {

        Self {
            response_len: 0,
            config_changed: false,
            outputs_changed: false,
            stop_watchdog_feed: false,
        }
    }
}

/// Stateful register bank and Modbus request dispatcher.

pub struct Server {
    pub bank: RegisterBank,
}

impl Server {
    /// Construct a server with a supplied address/configuration and chip UID.

    pub fn new(config: Config, uid: [u8; UID_BYTES]) -> Self {

        let mut outputs = Outputs::new_off();

        outputs.set_mask(config.startup_outputs);

        Self {
            bank: RegisterBank {
                config,
                outputs,
                discrete_inputs: 0,
                status: if config.is_configured() {
                    STATUS_CONFIGURED
                } else {
                    0
                },
                uid,
            },
        }
    }

    /// Process a complete RTU frame. `response` must be at least MAX_RESPONSE bytes.
    /// Bad CRCs, other device addresses, and unsupported broadcasts are silent.

    pub fn handle(&mut self, frame: &[u8], response: &mut [u8; MAX_RESPONSE]) -> Outcome {

        if frame.len() < 4 || !modbus::valid_crc(frame) {

            return Outcome::silent();
        }

        let address = frame[0];

        let function = frame[1];

        // UID-selective commissioning is a vendor extension sent as a broadcast.
        // A broadcast must never elicit a response.
        if address == 0 {

            if function != commissioning::FUNCTION {

                return Outcome::silent();
            }

            let payload = &frame[2..frame.len() - 2];

            if let Some(assignment) = commissioning::parse_assignment(payload) {

                if commissioning::applies(
                    &assignment,
                    &self.bank.uid,
                    self.bank.config.is_configured(),
                ) && self.bank.config.slave_address != assignment.address
                {

                    self.bank.config.slave_address = assignment.address;
                    self.bank.status |= STATUS_CONFIGURED;

                    return Outcome {
                        response_len: 0,
                        config_changed: true,
                        outputs_changed: false,
                        stop_watchdog_feed: false,
                    };
                }
            }

            return Outcome::silent();
        }

        if address != self.bank.config.slave_address || !(1..=247).contains(&address) {

            return Outcome::silent();
        }

        // Function 0x42 is an addressed, vendor-specific fault-injection
        // command. Acknowledge it with an echo response; the runtime stops
        // feeding only after that response has had a chance to leave the UART.
        if function == watchdog::STOP_FEED_FUNCTION {
            if !watchdog::is_stop_feed_command(frame) {
                return self.exception(address, function, Exception::IllegalDataValue, response);
            }

            response[0] = address;
            response[1] = function;
            return Outcome {
                response_len: modbus::append_crc(response, 2).unwrap_or(0),
                config_changed: false,
                outputs_changed: false,
                stop_watchdog_feed: true,
            };
        }

        let request = match modbus::parse(frame) {
            Ok(request) => request,
            Err(exception) => {

                // The parser returns DeviceFailure for bad CRC, already filtered.
                return self.exception(address, function, exception, response);
            }
        };

        let mut outputs_changed = false;

        let payload_len = match request {
            Request::ReadCoils { start, count } => {

                if count == 0 || count > 8 || start >= 8 || start + count > 8 {

                    return self.exception(
                        address,
                        function,
                        Exception::IllegalDataAddress,
                        response,
                    );
                }

                let mask = self.bank.outputs.mask() >> start;

                response[0] = address;

                response[1] = function;

                response[2] = 1;

                response[3] = mask & ((1u16 << count) - 1) as u8;

                4
            }
            Request::ReadDiscreteInputs { start, count } => {
                let end = start.checked_add(count);
                if start >= crate::registers::DISCRETE_INPUT_COUNT
                    || end.is_none_or(|end| end > crate::registers::DISCRETE_INPUT_COUNT) {
                    return self.exception(
                        address,
                        function,
                        Exception::IllegalDataAddress,
                        response,
                    );
                }

                response[0] = address;
                response[1] = function;
                response[2] = 1;
                response[3] = (self.bank.discrete_inputs >> start) & ((1u16 << count) - 1) as u8;
                4
            }
            Request::ReadHolding { start, count } | Request::ReadInput { start, count } => {

                let max_count = ((MAX_RESPONSE - 5) / 2) as u16;

                if count > max_count {

                    return self.exception(
                        address,
                        function,
                        Exception::IllegalDataValue,
                        response,
                    );
                }

                let end = match start.checked_add(count) {
                    Some(end) => end,
                    None => {
                        return self.exception(
                            address,
                            function,
                            Exception::IllegalDataAddress,
                            response,
                        );
                    }
                };

                let available = if function == 0x03 {

                    crate::registers::HOLDING_COUNT
                } else {

                    crate::registers::INPUT_COUNT
                };

                if end > available {

                    return self.exception(
                        address,
                        function,
                        Exception::IllegalDataAddress,
                        response,
                    );
                }

                response[0] = address;

                response[1] = function;

                response[2] = (count * 2) as u8;

                for offset in 0..count {

                    let register = if function == 0x03 {

                        self.bank.holding(start + offset)
                    } else {

                        self.bank.input(start + offset)
                    };

                    let value = match register {
                        Some(value) => value,
                        None => {
                            return self.exception(
                                address,
                                function,
                                Exception::IllegalDataAddress,
                                response,
                            );
                        }
                    };

                    let i = 3 + (offset as usize) * 2;

                    response[i..i + 2].copy_from_slice(&value.to_be_bytes());
                }

                3 + (count as usize) * 2
            }
            Request::WriteCoil { .. } | Request::WriteCoils { .. } => {

                let previous_mask = self.bank.outputs.mask();

                if let Err(exception) = modbus::apply_coils(request, &mut self.bank.outputs) {

                    return self.exception(address, function, exception, response);
                }

                outputs_changed = previous_mask != self.bank.outputs.mask();

                response[0] = address;

                response[1] = function;

                // 0x05 echoes coil address and value; 0x0F echoes start and count.
                // In both cases these are the first four payload bytes.
                response[2..6].copy_from_slice(&frame[2..6]);

                6
            }
        };

        let response_len = modbus::append_crc(response, payload_len).unwrap_or(0);

        Outcome {
            response_len,
            config_changed: false,
            outputs_changed,
            stop_watchdog_feed: false,
        }
    }

    fn exception(
        &self,
        address: u8,
        function: u8,
        error: Exception,
        response: &mut [u8; MAX_RESPONSE],
    ) -> Outcome {

        response[0] = address;

        response[1] = function | 0x80;

        response[2] = error as u8;

        Outcome {
            response_len: modbus::append_crc(response, 3).unwrap_or(0),
            config_changed: false,
            outputs_changed: false,
            stop_watchdog_feed: false,
        }
    }
}

#[cfg(test)]

mod tests {

    use super::*;

    fn request(bytes: &[u8]) -> ([u8; 128], usize) {

        let mut out = [0; 128];

        out[..bytes.len()].copy_from_slice(bytes);

        let len = modbus::append_crc(&mut out, bytes.len()).unwrap();

        (out, len)
    }

    fn server() -> Server {

        let mut config = Config::default();

        config.slave_address = 247;

        Server::new(config, [0xA5; UID_BYTES])
    }

    #[test]

    fn reads_uid_at_factory_address() {

        let mut server = server();

        let (req, len) = request(&[247, 0x04, 0, 0, 0, 9]);

        let mut reply = [0; MAX_RESPONSE];

        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 23);

        assert_eq!(reply[2], 18);

        assert_eq!(&reply[9..21], &[0xA5; UID_BYTES]);

        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]
    fn reads_holding_registers_in_modbus_byte_order() {
        let mut server = server();
        server.bank.config.comms_timeout_ms = 0x1234;

        let (req, len) = request(&[247, 0x03, 0, 1, 0, 3]);
        let mut reply = [0; MAX_RESPONSE];
        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 11);
        assert_eq!(&reply[..9], &[247, 0x03, 6, 0, 0, 0x12, 0x34, 0, 0]);
        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]
    fn reads_pc7_as_discrete_input_zero() {
        let mut server = server();
        server.bank.discrete_inputs = 1;

        let (req, len) = request(&[247, 0x02, 0, 0, 0, 1]);
        let mut reply = [0; MAX_RESPONSE];
        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 6);
        assert_eq!(&reply[..4], &[247, 0x02, 1, 1]);
        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]
    fn rejects_unmapped_discrete_input() {
        let mut server = server();
        let (req, len) = request(&[247, 0x02, 0, 1, 0, 1]);
        let mut reply = [0; MAX_RESPONSE];
        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(
            &reply[..3],
            &[247, 0x82, Exception::IllegalDataAddress as u8]
        );
        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]
    fn rejects_register_ranges_that_cross_the_map_boundary() {
        let mut server = server();
        let (req, len) = request(&[247, 0x04, 0, 8, 0, 2]);
        let mut reply = [0; MAX_RESPONSE];
        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 5);
        assert_eq!(&reply[..3], &[247, 0x84, Exception::IllegalDataAddress as u8]);
        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]

    fn changes_address_by_uid_without_reply() {

        let mut server = server();

        let mut payload = [0u8; 16];

        payload[0] = 0;

        payload[1] = commissioning::FUNCTION;

        payload[2] = commissioning::ASSIGN_UID;

        payload[3..15].fill(0xA5);

        payload[15] = 7;

        let (req, len) = request(&payload);

        let mut reply = [0; MAX_RESPONSE];

        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 0);

        assert!(outcome.config_changed);

        assert_eq!(server.bank.config.slave_address, 7);
    }

    #[test]

    fn writes_and_reads_coils() {

        let mut server = server();

        let mut reply = [0; MAX_RESPONSE];

        let (req, len) = request(&[247, 0x05, 0, 2, 0xFF, 0]);

        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 8);

        assert!(outcome.outputs_changed);

        assert_eq!(server.bank.outputs.mask(), 4);

        let (req, len) = request(&[247, 0x01, 0, 0, 0, 8]);

        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 6);

        assert_eq!(reply[3], 4);
    }

    #[test]
    fn stop_watchdog_command_is_acknowledged_and_reported() {
        let mut server = server();
        let (req, len) = request(&[247, watchdog::STOP_FEED_FUNCTION]);
        let mut reply = [0; MAX_RESPONSE];

        let outcome = server.handle(&req[..len], &mut reply);

        assert_eq!(outcome.response_len, 4);
        assert!(outcome.stop_watchdog_feed);
        assert_eq!(&reply[..2], &[247, watchdog::STOP_FEED_FUNCTION]);
        assert!(modbus::valid_crc(&reply[..outcome.response_len]));
    }

    #[test]
    fn malformed_or_broadcast_stop_watchdog_commands_do_not_trigger() {
        let mut server = server();
        let mut reply = [0; MAX_RESPONSE];

        let (req, len) = request(&[247, watchdog::STOP_FEED_FUNCTION, 1]);
        let outcome = server.handle(&req[..len], &mut reply);
        assert!(!outcome.stop_watchdog_feed);
        assert_eq!(reply[1], watchdog::STOP_FEED_FUNCTION | 0x80);

        let (req, len) = request(&[0, watchdog::STOP_FEED_FUNCTION]);
        let outcome = server.handle(&req[..len], &mut reply);
        assert_eq!(outcome.response_len, 0);
        assert!(!outcome.stop_watchdog_feed);
    }
}
