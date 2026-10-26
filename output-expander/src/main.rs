#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::gpio::{Level, Output};
use hal::timer::low_level::Timer;
use hal::time::Hertz;
use hal::interrupt::InterruptExt;
use hal::pac;

use core::sync::atomic::{AtomicBool, Ordering};
use panic_halt as _;

// Flag set by TIM2 interrupt
static TIMER_TICK: AtomicBool = AtomicBool::new(false);

// TIM2 interrupt handler
#[qingke_rt::interrupt]
fn TIM2() {
    // Clear timer interrupt flag
    pac::TIM2.intfr().modify(|r| r.set_uif(false));

    // Signal main loop
    TIMER_TICK.store(true, Ordering::Relaxed);
}

#[qingke_rt::entry]
fn main() -> ! {
    let p = hal::init(hal::Config::default());

    // Active-low LED on PC1
    let mut led =
        Output::new(p.PC1, Level::High, Default::default());

    // Configure TIM2 for 10 Hz (100 ms)
    let timer = Timer::new(p.TIM2);

    timer.set_frequency(Hertz(1));
    timer.clear_update_interrupt();
    timer.enable_update_interrupt(true);

    // Enable TIM2 interrupt in PFIC
    unsafe {
        hal::interrupt::TIM2.enable();
    }

    timer.start();

    loop {
        // Check whether timer interrupt occurred
        if TIMER_TICK.load(Ordering::Relaxed) {
            TIMER_TICK.store(false, Ordering::Relaxed);
            led.toggle();
        }

        // Other processing can happen here
    }
}