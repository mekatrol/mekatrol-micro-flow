#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::delay::Delay;
use hal::gpio::{Level, Output};
use panic_halt as _;

/// LED1 is active-low: VCC -> R2 -> LED1 -> PC1.
#[qingke_rt::entry]
fn main() -> ! {
    let peripherals = hal::init(hal::Config::default());
    let mut led = Output::new(peripherals.PC1, Level::High, Default::default());
    let mut delay = Delay;

    loop {
        led.set_low();
        delay.delay_ms(100);

        led.set_high();
        delay.delay_ms(100);
    }
}
