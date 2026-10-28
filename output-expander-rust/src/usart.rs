use crate::irq_rx::{self, RxEvent};
use ch32_hal as hal;

/// Capture one USART byte or error and hand it to the foreground ring buffer.
///
/// Reading STATR before DATAR follows the peripheral's required clearing
/// sequence. Parsing and transmission are intentionally excluded because an
/// interrupt must remain short enough not to starve the foreground watchdog.
#[hal::interrupt]
unsafe fn USART1() {
    let uart = hal::pac::USART1;

    // Snapshot all status bits before DATAR clears receive/error conditions.
    let status = uart.statr().read();

    // There is an error if and of the status error bits are set
    let has_error = status.pe() || status.fe() || status.ne() || status.ore();

    // Process only RXNE or receive-error conditions, return for others.
    if !status.rxne() && !has_error {
        return;
    }

    // Reading DATAR consumes RXNE. A byte accompanied by an error is marked
    // unusable rather than allowing corrupt data into the frame decoder.
    let value = uart.datar().read().dr() as u8;

    // Queue the received byte or UART error for foreground processing.
    irq_rx::push_from_interrupt(if has_error {
        RxEvent::Error {}
    } else {
        RxEvent::Byte { value }
    });
}

/// Enable USART receive and error sources after every consumer is initialized.
///
/// # Safety
///
/// The USART peripheral, global queue, and foreground receiver must be ready
/// before unmasking this interrupt, otherwise an early byte could be lost.
pub unsafe fn enable_usart1_rx_interrupts() {
    use hal::interrupt::InterruptExt;
    let uart = hal::pac::USART1;

    // RXNE delivers ordinary bytes; PE reports parity errors even though the
    // configured 8N1 link should never intentionally generate parity.
    uart.ctlr1().modify(|w| {
        w.set_rxneie(true);
        w.set_peie(true);
    });
    // EIE adds framing, noise, and overrun errors so every broken receive
    // stream forces the current packet to be discarded.
    uart.ctlr3().modify(|w| w.set_eie(true));

    // SAFETY: handler, UART, and queue state are fully initialized above.
    unsafe {
        hal::interrupt::USART1.enable();
    }
}
