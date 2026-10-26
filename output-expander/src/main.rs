#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::gpio::{Level, Output};
use hal::timer::low_level::Timer;
use hal::time::Hertz;
use hal::interrupt::InterruptExt;
use hal::pac;

use panic_halt as _;

// Track LED state.
// false = LED OFF, true = LED ON
static mut LED_ON: bool = false;

// TIM2 interrupt handler
#[qingke_rt::interrupt]
fn TIM2() {
    // Clear timer interrupt flag
    pac::TIM2.intfr().modify(|r| r.set_uif(false));

    // Toggle LED state
    // PC1 is active-low:
    // LOW  = LED ON
    // HIGH = LED OFF
    unsafe {
        let next = !LED_ON;
        LED_ON = next;

        if next {
            // PC1 LOW (LED ON)
            pac::GPIOC.bshr().write(|w| w.set_br(1, true));
        } else {
            // PC1 HIGH (LED OFF)
            pac::GPIOC.bshr().write(|w| w.set_bs(1, true));
        }
    }
}

#[qingke_rt::entry]
fn main() -> ! {
    let p = hal::init(hal::Config::default());

    // Active-low LED on PC1
    let _led =
        Output::new(p.PC1, Level::High, Default::default());

    // Configure TIM2 for 1 Hz (1000 ms)
    let timer = Timer::new(p.TIM2);

    timer.set_frequency(Hertz(10));
    timer.clear_update_interrupt();
    timer.enable_update_interrupt(true);

    // Enable TIM2 interrupt in PFIC
    unsafe {
        hal::interrupt::TIM2.enable();
    }

    timer.start();

    loop {
        // Main program is free to perform other tasks.
        // LED timing is handled entirely by TIM2.
    }
}