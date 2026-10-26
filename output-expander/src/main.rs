//! Initial Modbus RTU integration for CH32V003.
//!
//! This bring-up version receives supported request sizes using blocking UART.
//! It is NOT a complete timing-based RTU receiver: see notes accompanying file.
#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::delay::Delay;
use hal::gpio::{Level, Output};
use hal::usart::{Config as UartConfig, DataBits, Parity, StopBits, Uart};
use output_expander::board::RS485_BAUD;
use output_expander::config::Config as DeviceConfig;
use output_expander::modbus::MAX_RESPONSE;
use output_expander::server::Server;
use output_expander::uid;
use panic_halt as _;

// Maximum supported request: broadcast UID assignment = 18 bytes.
// Other requests are 8 bytes, except write-multiple-coils (10 bytes for 8 coils).
const MAX_REQUEST: usize = 18;

/// Determine the full length of a supported RTU request.
/// Return None until enough header bytes have arrived.
fn request_length(bytes: &[u8]) -> Option<usize> {
    if bytes.len() < 2 {
        return None;
    }

    match bytes[1] {
        0x01 | 0x03 | 0x04 | 0x05 => Some(8),
        0x0F => {
            if bytes.len() < 7 {
                None
            } else {
                Some(9 + bytes[6] as usize)
            }
        }
        0x41 if bytes[0] == 0 => Some(18),
        // Unsupported functions cannot be framed by length here.
        _ => None,
    }
}

#[qingke_rt::entry]

fn main() -> ! {
    let p = hal::init(hal::Config::default());

    // Existing prototype PCB: separate driver enable and receiver enable.
    // Both low = receive, both high = transmit.
    let mut rs485_de = Output::new(p.PC6, Level::Low, Default::default());

    let mut rs485_re = Output::new(p.PC7, Level::Low, Default::default());

    let mut uart_config = UartConfig::default();

    uart_config.baudrate = RS485_BAUD;

    uart_config.data_bits = DataBits::DataBits8;

    uart_config.parity = Parity::ParityEven;

    uart_config.stop_bits = StopBits::STOP1;

    // Verify HAL parity/data-bits mapping against the pinned ch32-hal revision.
    let mut uart = Uart::new_blocking(p.USART1, p.PD6, p.PD5, uart_config).unwrap();

    let mut config = DeviceConfig::default();

    // Temporary until persistent flash loading is implemented.
    config.slave_address = 247;

    // Keep physical outputs safe; this demo does not drive the ULN2803 pins.
    config.startup_outputs = 0;

    let mut server = Server::new(config, uid::read());

    let mut request = [0u8; MAX_REQUEST];

    let mut response = [0u8; MAX_RESPONSE];

    loop {
        // Blocking receive: no hardware timeout or t1.5/t3.5 gap handling yet.
        // Resynchronization after noise/incomplete frames is not guaranteed.
        let mut len = 0usize;

        let mut expected = None;

        loop {
            if len == MAX_REQUEST {
                break;
            }

            if uart.blocking_read(&mut request[len..len + 1]).is_err() {
                break;
            }

            len += 1;

            expected = request_length(&request[..len]);

            if let Some(n) = expected {
                if n > MAX_REQUEST || len >= n {
                    break;
                }
            }
        }

        let Some(expected_len) = expected else {
            continue;
        };

        if expected_len != len {
            continue;
        }

        let outcome = server.handle(&request[..len], &mut response);

        if outcome.outputs_changed {
            // TODO: Apply server.bank.outputs.mask() to the actual eight GPIOs.
            // Do not connect loads expecting Modbus writes to switch them yet.
        }

        if outcome.config_changed {
            // TODO: Save server.bank.config to reserved flash page.
            // Address changes currently last only until reset.
        }

        if outcome.response_len == 0 {
            continue;
        }

        // Allow the host's RS-485 driver to release the bus.
        Delay.delay_us(1_000);

        rs485_re.set_high();

        rs485_de.set_high();

        // Wait for the UART to finish sending the final stop bit before
        // returning to receive mode.
        let _ = uart.blocking_write(&response[..outcome.response_len]);

        let _ = uart.blocking_flush();

        rs485_de.set_low();

        rs485_re.set_low();
    }
}
