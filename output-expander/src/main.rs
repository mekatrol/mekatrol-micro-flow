#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::delay::Delay;
use hal::gpio::{Level, Output};
use hal::usart::{Config, DataBits, Parity, StopBits, Uart};

use panic_halt as _;

const BAUD_RATE: u32 = 19_200;

#[qingke_rt::entry]
fn main() -> ! {
    let p = hal::init(hal::Config::default());

    // The current PCB routes the transceiver's DE and /RE inputs separately.
    // Drive both low before enabling the UART so the RS-485 bus starts in
    // receive mode. A future PCB may connect both inputs to PC6.
    let mut rs485_de = Output::new(p.PC6, Level::Low, Default::default());
    let mut rs485_re = Output::new(p.PC7, Level::Low, Default::default());

    let mut uart_config = Config::default();
    uart_config.baudrate = BAUD_RATE;

    // Use the conventional serial-monitor default for initial hardware
    // bring-up. Modbus can switch this to 8E1 when framing is implemented.
    uart_config.data_bits = DataBits::DataBits8;
    uart_config.parity = Parity::ParityNone;
    uart_config.stop_bits = StopBits::STOP1;

    // USART1's selected CH32V003 mapping is RX=PD6 and TX=PD5.
    let mut uart = Uart::new_blocking(p.USART1, p.PD6, p.PD5, uart_config).unwrap();
    let mut line = [0_u8; 64];
    let mut line_len = 0;

    loop {
        // Collect a complete serial-monitor message before replying. Echoing
        // each byte immediately would contend with a host that is still
        // transmitting the remaining bytes in the same RS-485 transaction.
        if uart
            .blocking_read(&mut line[line_len..line_len + 1])
            .is_err()
        {
            line_len = 0;
            continue;
        }
        line_len += 1;

        if line[line_len - 1] != b'\n' && line_len < line.len() {
            continue;
        }

        // Allow the host adapter to release its driver after the final stop
        // bit. At 19,200 8N1 one character is about 521 microseconds.
        Delay.delay_us(1_000);

        // DE=HIGH enables the driver; /RE=HIGH disables the receiver. Keeping
        // the pins at the same level also matches the planned single-PC6 PCB.
        rs485_de.set_high();
        rs485_re.set_high();

        if uart.blocking_write(&line[..line_len]).is_ok() {
            // Do not release the RS-485 driver until the final stop bit has
            // physically left the USART shift register.
            let _ = uart.blocking_flush();
        }

        rs485_de.set_low();
        rs485_re.set_low();
        line_len = 0;
    }
}
