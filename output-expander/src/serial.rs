use ch32_hal as hal;

use crate::board::RS485_BAUD;
use crate::rs485::Rs485Direction;
use hal::Peri;
use hal::delay::Delay;
use hal::mode::Blocking;
use hal::peripherals::USART1;
use hal::usart::{Config as UartConfig, DataBits, Parity, StopBits, Uart};

/// Hardware driver for the USART1-connected RS-485 interface.
///
/// WHAT:
/// Owns the UART peripheral and the RS-485 direction controller.
///
/// WHY:
/// The main application should not need to configure UART
/// parameters, perform UART reads or writes, or manage DE /RE.
///
/// This implementation is currently blocking.
/// Interrupt-driven reception will be added later.
pub struct Serial<'d> {
    uart: Uart<'d, USART1, Blocking>,
    direction: Rs485Direction<'d>,
}

impl<'d> Serial<'d> {
    /// Initialize USART1 and construct the serial driver.
    ///
    /// WHAT:
    /// Configures USART1 for Modbus RTU at 19200 baud, 8E1.
    ///
    /// WHY:
    /// UART configuration belongs with the hardware driver,
    /// rather than being repeated in application code.
    ///
    /// The caller supplies:
    /// - USART1 peripheral ownership
    /// - RX pin PD6
    /// - TX pin PD5
    /// - RS-485 direction controller
    ///
    /// NOTE:
    /// This constructor currently panics if UART configuration
    /// fails, matching the previous main.rs behaviour.
    pub fn new(
        usart: Peri<'d, hal::peripherals::USART1>,
        rx: Peri<'d, hal::peripherals::PD6>,
        tx: Peri<'d, hal::peripherals::PD5>,
        direction: Rs485Direction<'d>,
    ) -> Self {
        // Configure Modbus RTU serial communication.
        //
        // 19200 baud, 8 data bits, even parity, 1 stop bit.
        let mut config = UartConfig::default();

        config.baudrate = RS485_BAUD;
        config.data_bits = DataBits::DataBits8;
        config.parity = Parity::ParityEven;
        config.stop_bits = StopBits::STOP1;

        // Initialize the hardware UART.
        //
        // RX and TX pins are assigned here so main.rs does
        // not need to know the UART pin configuration.
        let uart = Uart::new_blocking(usart, rx, tx, config).unwrap();

        // Ensure the transceiver begins in receive mode.
        //
        // WHY:
        // The shared RS-485 bus must not be driven unless
        // this device is actively transmitting a response.
        let mut direction = direction;
        direction.receive();

        Self { uart, direction }
    }

    /// Receive exactly one byte from USART1.
    ///
    /// This currently blocks until a byte arrives or
    /// the UART reports an error.
    ///
    /// The future interrupt-driven receiver will remove
    /// this blocking behaviour.
    pub fn read_byte(&mut self) -> Result<u8, hal::usart::Error> {
        let mut byte = [0u8; 1];

        self.uart.blocking_read(&mut byte)?;

        Ok(byte[0])
    }

    /// Transmit a complete Modbus RTU response.
    ///
    /// Handles the RS-485 turnaround delay, transmitter
    /// enable, UART transmission, and return to receive mode.
    pub fn write_response(&mut self, response: &[u8]) -> Result<(), hal::usart::Error> {
        // Broadcast requests and some commands do not
        // require a response.
        if response.is_empty() {
            return Ok(());
        }

        // Preserve the existing turnaround delay.
        //
        // Proper RTU t3.5 timing will be implemented later.
        Delay.delay_us(1_000);

        // Enable the RS-485 transmitter.
        self.direction.transmit();

        // Write the response bytes.
        let result = self.uart.blocking_write(response);

        // Wait for the final stop bit to leave the UART.
        let flush_result = self.uart.blocking_flush();

        // Always release the RS-485 bus, including if
        // UART transmission reported an error.
        self.direction.receive();

        result?;
        flush_result
    }
}
