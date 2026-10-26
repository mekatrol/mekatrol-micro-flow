// This binary supplies its own embedded entry point instead of desktop `main`.
#![no_main]
// Bare-metal firmware cannot use Rust's operating-system-backed standard lib.
#![no_std]

// Give the hardware abstraction layer a short, readable local name.
use ch32_hal as hal;

// Import only the HAL types used below, keeping hardware roles visible.
use hal::delay::Delay;
use hal::gpio::{Level, Output};
use hal::usart::{Config, DataBits, Parity, StopBits, Uart};

// Link a panic handler that stops execution, which is safer than continuing
// after an unrecoverable fault on an output-controlling device.
use panic_halt as _;

/// Temporary serial-monitor speed used by this hardware bring-up program.

const BAUD_RATE: u32 = 19_200;

// This attribute installs the function as the reset-time firmware entry point.
#[qingke_rt::entry]
/// Initialise the UART and echo complete lines over half-duplex RS-485.
///
/// The never type `!` documents that embedded firmware runs forever.

fn main() -> ! {

    // Initialise clocks and peripherals, then take unique ownership of them.
    // Rust's ownership prevents the same hardware block being configured twice.
    let p = hal::init(hal::Config::default());

    // The current PCB routes the transceiver's DE and /RE inputs separately.
    // Drive both low before enabling the UART so the RS-485 bus starts in
    // receive mode. A future PCB may connect both inputs to PC6.
    let mut rs485_de = Output::new(p.PC6, Level::Low, Default::default());

    // `/RE` is active-low, so low enables reception at startup.
    let mut rs485_re = Output::new(p.PC7, Level::Low, Default::default());

    // Begin with the HAL's known-good UART settings, then customise framing.
    let mut uart_config = Config::default();

    // Set the number of serial bits transmitted each second.
    uart_config.baudrate = BAUD_RATE;

    // Use the conventional serial-monitor default for initial hardware
    // bring-up. Modbus can switch this to 8E1 when framing is implemented.
    uart_config.data_bits = DataBits::DataBits8;

    // Disable parity for compatibility with ordinary serial monitors.
    uart_config.parity = Parity::ParityNone;

    // Use one stop bit, producing the familiar 8N1 serial format.
    uart_config.stop_bits = StopBits::STOP1;

    // USART1's selected CH32V003 mapping is RX=PD6 and TX=PD5.
    // Blocking mode keeps this initial diagnostic program straightforward;
    // unwrap halts on an impossible/unsupported peripheral configuration.
    let mut uart = Uart::new_blocking(p.USART1, p.PD6, p.PD5, uart_config).unwrap();

    // Hold one line in fixed stack storage because no allocator is available.
    let mut line = [0_u8; 64];

    // Track how much of the array currently contains received data.
    let mut line_len = 0;

    // Firmware has no operating system to return to, so service the bus forever.
    loop {

        // Collect a complete serial-monitor message before replying. Echoing
        // each byte immediately would contend with a host that is still
        // transmitting the remaining bytes in the same RS-485 transaction.
        if uart
            // Borrow exactly the next free one-byte slot for the blocking read.
            .blocking_read(&mut line[line_len..line_len + 1])
            .is_err()
        {

            // Discard a partial line after a UART error to avoid echoing damage.
            line_len = 0;

            // Restart the loop and wait for a fresh message.
            continue;
        }

        // The read succeeded, so include that byte in the buffered length.
        line_len += 1;

        // Keep receiving until newline or a full buffer ends the message.
        if line[line_len - 1] != b'\n' && line_len < line.len() {

            continue;
        }

        // Allow the host adapter to release its driver after the final stop
        // bit. At 19,200 8N1 one character is about 521 microseconds.
        Delay.delay_us(1_000);

        // DE=HIGH enables the driver; /RE=HIGH disables the receiver. Keeping
        // the pins at the same level also matches the planned single-PC6 PCB.
        rs485_de.set_high();

        // `/RE=HIGH` disables local reception to avoid hearing our own reply.
        rs485_re.set_high();

        // Send only the filled portion, not unused zeroes in the fixed array.
        if uart.blocking_write(&line[..line_len]).is_ok() {

            // Do not release the RS-485 driver until the final stop bit has
            // physically left the USART shift register.
            // Ignore the flush result here because direction must be restored
            // regardless; production code can count/report this fault.
            let _ = uart.blocking_flush();
        }

        // Disable the transmitter so another RS-485 participant may drive.
        rs485_de.set_low();

        // Re-enable the receiver in preparation for the next request.
        rs485_re.set_low();

        // Reuse the line buffer from index zero; clearing bytes is unnecessary.
        line_len = 0;
    }
}
