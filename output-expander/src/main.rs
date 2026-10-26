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
use output_expander::rs485::Rs485Direction;
use output_expander::server::Server;
use output_expander::uart::{MAX_REQUEST, request_length};
use output_expander::uid;
use panic_halt as _;

#[qingke_rt::entry]

fn main() -> ! {
    let p = hal::init(hal::Config::default());

    // Evaluation board:
    // PC6 controls DE.
    // PC7 controls the active-low receiver enable (/RE).
    //
    // Both signals start LOW so the transceiver begins
    // in receive mode without driving the RS-485 bus.
    let de = Output::new(p.PC6, Level::Low, Default::default());
    let re = Output::new(p.PC7, Level::Low, Default::default());

    let mut rs485 = Rs485Direction::new(de, Some(re));

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

        // Enable the RS-485 transmitter before writing the reply.
        rs485.transmit();

        // Wait for the UART to finish sending the final stop bit before
        // returning to receive mode.
        let _ = uart.blocking_write(&response[..outcome.response_len]);

        let _ = uart.blocking_flush();

        // Return to receive mode only after UART transmission
        // has completed, including the final stop bit.
        rs485.receive();
    }
}
