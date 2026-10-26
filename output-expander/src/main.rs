//! Initial Modbus RTU integration for CH32V003.
//!
//! This bring-up version uses blocking UART reception.
//!
//! UART transmission and RS-485 direction control are handled
//! by the Serial driver rather than directly by main.rs.
//!
//! IMPORTANT:
//! This is not yet a fully interrupt-driven Modbus RTU implementation.
//! Proper frame-gap timing and nonblocking reception will be
//! implemented in a later step.

#![no_main]
#![no_std]

// Hardware abstraction layer for the CH32V003.
use ch32_hal as hal;

// GPIO types needed to initialize RS-485 direction pins.
use hal::gpio::{Level, Output};

use output_expander::config::Config as DeviceConfig;

// Maximum response size supported by the Modbus server.
use output_expander::modbus::MAX_RESPONSE;

// Hardware abstraction for RS-485 direction control.
use output_expander::rs485::Rs485Direction;

// Serial driver encapsulating UART communication and RS-485 control.
use output_expander::serial::Serial;

// Modbus server responsible for interpreting complete requests.
use output_expander::server::Server;

// UART framing helpers.
use output_expander::uart::{MAX_REQUEST, request_length};

// Device silicon unique identifier.
use output_expander::uid;

// Stop execution if an unrecoverable panic occurs.
use panic_halt as _;

#[qingke_rt::entry]
fn main() -> ! {
    // Initialize the microcontroller hardware abstraction layer.
    //
    // This configures clocks and returns ownership of the
    // available hardware peripherals.
    let p = hal::init(hal::Config::default());

    // ---------------------------------------------------------
    // RS-485 direction control
    // ---------------------------------------------------------
    //
    // Evaluation board:
    // PC6 = Driver Enable (DE)
    // PC7 = Receiver Enable (/RE, active LOW)
    //
    // Both pins start LOW:
    // - Transmitter disabled
    // - Receiver enabled
    //
    // WHY:
    // Starting in receive mode prevents the device from
    // unintentionally driving the shared RS-485 bus.
    //
    // Production PCB:
    // DE and /RE are electrically connected to PC6.
    // That configuration will use None for the second pin.

    let direction = Rs485Direction::new(
        Output::new(p.PC6, Level::Low, Default::default()),
        Some(Output::new(p.PC7, Level::Low, Default::default())),
    );

    // ---------------------------------------------------------
    // Serial communication driver
    // ---------------------------------------------------------
    //
    // WHAT:
    // Initialize USART1 and transfer ownership of the UART
    // peripheral, RX/TX pins, and RS-485 direction controller.
    //
    // WHY:
    // All UART configuration is now encapsulated in serial.rs.
    //
    // main.rs no longer needs to know the baud rate, parity,
    // stop bits, or UART initialization API.
    let mut serial = Serial::new(p.USART1, p.PD6, p.PD5, direction);

    // ---------------------------------------------------------
    // Application configuration
    // ---------------------------------------------------------

    // Load default configuration.
    //
    // TODO:
    // Replace this with configuration loaded from flash.
    let mut config = DeviceConfig::default();

    // Factory Modbus address.
    //
    // Address 247 is currently used until commissioning
    // assigns a different device address.
    config.slave_address = 247;

    // Keep all eight outputs disabled at startup.
    //
    // Physical output GPIO handling is not implemented yet.
    config.startup_outputs = 0;

    // ---------------------------------------------------------
    // Modbus server
    // ---------------------------------------------------------

    // Create the Modbus server using device configuration
    // and the MCU's unique silicon identifier.
    let mut server = Server::new(config, uid::read());

    // Fixed-size request buffer.
    //
    // WHY:
    // The firmware operates without a heap allocator.
    let mut request = [0u8; MAX_REQUEST];

    // Fixed-size response buffer.
    let mut response = [0u8; MAX_RESPONSE];

    // ---------------------------------------------------------
    // Main application loop
    // ---------------------------------------------------------
    //
    // TEMPORARY IMPLEMENTATION:
    //
    // The loop currently blocks while receiving UART data.
    //
    // Future implementation:
    // 1. Process completed Modbus packets.
    // 2. Update WS2812 status LED.
    // 3. Service the watchdog when system health permits.
    //
    // UART reception will move to interrupts so these
    // operations can run without being blocked.

    loop {
        // Number of bytes received for the current request.
        let mut len = 0usize;

        // Expected total request length.
        //
        // None means the header is incomplete or unsupported.
        let mut expected = None;

        // -----------------------------------------------------
        // Receive a Modbus request
        // -----------------------------------------------------

        loop {
            // Prevent writing beyond the request buffer.
            if len == MAX_REQUEST {
                break;
            }

            // Read one byte using the Serial driver.
            //
            // WHY:
            // main.rs should not know the details of how
            // the UART hardware receives data.
            //
            // WARNING:
            // This operation currently blocks.
            match serial.read_byte() {
                Ok(byte) => {
                    request[len] = byte;
                }

                Err(_) => {
                    // Abort the current request if UART
                    // reception reports an error.
                    break;
                }
            }

            // Record the newly received byte.
            len += 1;

            // Determine the expected request length.
            //
            // This is temporary length-based framing.
            // Proper RTU timing-based framing comes later.
            expected = request_length(&request[..len]);

            // Stop when the complete request has arrived
            // or the advertised size exceeds our buffer.
            if let Some(n) = expected {
                if n > MAX_REQUEST || len >= n {
                    break;
                }
            }
        }

        // Reject requests whose length could not be determined.
        let Some(expected_len) = expected else {
            continue;
        };

        // Reject incomplete or incorrectly sized requests.
        if expected_len != len {
            continue;
        }

        // -----------------------------------------------------
        // Process Modbus request
        // -----------------------------------------------------

        // Pass the complete request to the Modbus server.
        //
        // The server validates the CRC, checks the slave
        // address, executes supported commands, and builds
        // the response.
        let outcome = server.handle(&request[..len], &mut response);

        // -----------------------------------------------------
        // Apply application changes
        // -----------------------------------------------------

        if outcome.outputs_changed {
            // TODO:
            // Apply server.bank.outputs.mask() to the
            // eight physical output GPIOs.
            //
            // Until implemented, Modbus writes only
            // change the logical output state.
        }

        if outcome.config_changed {
            // TODO:
            // Save server.bank.config to reserved flash.
            //
            // Until implemented, configuration changes
            // will be lost when the device resets.
        }

        // -----------------------------------------------------
        // Transmit Modbus response
        // -----------------------------------------------------

        // Some Modbus requests must not generate a response.
        //
        // Examples include broadcast commissioning commands.
        if outcome.response_len == 0 {
            continue;
        }

        // Send the response using the Serial driver.
        //
        // The driver is responsible for:
        // 1. RS-485 turnaround delay.
        // 2. Enabling the transmitter.
        // 3. Writing response bytes.
        // 4. Waiting for transmission completion.
        // 5. Returning the transceiver to receive mode.
        //
        // WHY:
        // These are hardware communication responsibilities
        // and do not belong in the application main loop.
        let _ = serial.write_response(&response[..outcome.response_len]);
    }
}
