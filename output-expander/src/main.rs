//! Interrupt-driven proprietary RS-485 firmware for the CH32V003.
//!
//! USART1 interrupts do the minimum work needed to preserve incoming bytes in
//! a circular queue. The foreground loop owns framing, command execution,
//! GPIO changes, RS-485 transmission, status indication, and watchdog feeds so
//! no interrupt can perform an unbounded or safety-sensitive operation.
#![no_main]
#![no_std]

use ch32_hal as hal;
use hal::gpio::{Input, Level, Output, Pull};
use output_expander::config::Config as DeviceConfig;
use output_expander::dispatcher::{Dispatcher, STATUS_CONFIG_FAULT, STATUS_UART_ERROR};
use output_expander::irq_rx::{self, RxEvent, RxQueueRead};
use output_expander::monotonic::Monotonic;
use output_expander::output_gpio::OutputGpios;
use output_expander::protocol::{self, FrameReceiver, ReceiveResult};
use output_expander::rs485::Rs485Direction;
use output_expander::serial::Serial;
use output_expander::status_led::{OFF, Rgb, pattern_color};
use output_expander::uid;
use output_expander::watchdog::IndependentWatchdog;
use output_expander::ws2812::Ws2812;
use panic_halt as _;

/// Conservative LED intensity used until EEPROM configuration is integrated.
const STATUS_LED_BRIGHTNESS: u8 = 32;

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
unsafe fn enable_usart1_rx_interrupts() {
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

#[hal::entry]
fn main() -> ! {
    // Run from the internal 48 MHz HSI because UART timing, SysTick conversion,
    // and the calibrated WS2812 waveform all depend on this clock rate.
    let mut hal_config = hal::Config::default();
    hal_config.rcc = hal::rcc::Config::SYSCLK_FREQ_48MHZ_HSI;
    let p = hal::init(hal_config);

    // WS2812 data idles LOW. PC1 ownership remains with the LED driver for the
    // entire program so another peripheral cannot disturb its waveform.
    let mut status_led = Ws2812::new(Output::new(p.PC1, Level::Low, Default::default()));

    // Drive every active-low channel HIGH/OFF before constructing application
    // state. This prevents a startup glitch from energizing an attached load.
    let mut outputs = OutputGpios::new([
        Output::new(p.PC0, Level::High, Default::default()),
        Output::new(p.PC3, Level::High, Default::default()),
        Output::new(p.PC4, Level::High, Default::default()),
        Output::new(p.PC5, Level::High, Default::default()),
        Output::new(p.PD0, Level::High, Default::default()),
        Output::new(p.PD2, Level::High, Default::default()),
        Output::new(p.PD3, Level::High, Default::default()),
        Output::new(p.PD4, Level::High, Default::default()),
    ]);
    // Configure one free-running counter shared by status timing, UART event
    // timestamps, and bounded RS-485 turnaround delays.
    let timer = Monotonic::new();

    // DE and active-low /RE are electrically tied on the planned board. LOW
    // starts in receive mode so reset cannot accidentally drive the shared bus.
    let direction = Rs485Direction::new(Output::new(p.PC6, Level::Low, Default::default()));

    // PC7 is pulled up and becomes true only when the active-low button closes.
    let button = Input::new(p.PC7, Pull::Up);

    // Serial owns USART1, its pins, and transceiver direction so every response
    // waits for transmission-complete before returning the bus to receive mode.
    let mut serial = Serial::new(p.USART1, p.PD6, p.PD5, direction);

    // EEPROM loading is not integrated yet, so use safe factory defaults and
    // the immutable silicon UID to establish protocol state.
    let mut dispatcher = Dispatcher::new(DeviceConfig::default(), uid::read());

    // Apply startup state only after all physical outputs were first made safe.
    outputs.apply_mask(dispatcher.outputs.mask());

    // Both receive and response storage are fixed-size, making their complete
    // SRAM cost visible and preventing malformed traffic from allocating memory.
    let mut receiver = FrameReceiver::new();
    let mut wire_response = [0u8; protocol::MAX_WIRE];

    // Cache visible status to avoid retransmitting an identical WS2812 word on
    // every fast foreground iteration.
    let mut displayed_color = OFF;
    let mut command_flash_started = None;
    let mut error_present = false;
    let mut watchdog_feed_enabled = true;
    status_led.set_rgb(OFF);

    // Interrupts begin only after the queue, decoder, dispatcher, and response
    // buffer exist, closing the startup race with the first received byte.
    unsafe {
        enable_usart1_rx_interrupts();
    }

    // Start the independent watchdog last. The only feed point is at the end
    // of the foreground loop, so a stall in any work above causes a reset.
    let mut watchdog = IndependentWatchdog::start();
    loop {
        // Convert the command activity timestamp into milliseconds without
        // resetting the counter used by other subsystems.
        let now_ticks = timer.now_ticks();
        let command_elapsed_ms = command_flash_started
            .map(|started| now_ticks.wrapping_sub(started) / timer.ticks_per_us() / 1_000);
        if matches!(command_elapsed_ms, Some(elapsed) if elapsed >= 100) {
            // Stop giving activity priority after its documented 100 ms pulse.
            command_flash_started = None;
        }

        // Reset requests deliberately hold solid red while the watchdog counts
        // down; normal operation delegates heartbeat/error priority selection.
        let desired_color = if watchdog_feed_enabled {
            pattern_color(
                now_ticks / timer.ticks_per_us() / 1_000,
                STATUS_LED_BRIGHTNESS,
                error_present,
                command_elapsed_ms,
            )
        } else {
            Rgb(STATUS_LED_BRIGHTNESS, 0, 0)
        };
        if desired_color != displayed_color {
            // WS2812 transmission briefly masks interrupts, so send only when
            // the requested color actually changes.
            status_led.set_rgb(desired_color);
            displayed_color = desired_color;
        }

        // Drain every queued event before feeding the watchdog. This prevents a
        // sustained but manageable byte stream from overflowing the ring merely
        // because only one event was consumed per foreground iteration.
        loop {
            let event = match irq_rx::read_next_from_main() {
                RxQueueRead::Event(event) => event,
                RxQueueRead::Empty => break,
                RxQueueRead::Overflow => {
                    // Any dropped byte makes packet boundaries and CRC contents
                    // untrustworthy. Discard through the next zero delimiter.
                    receiver.reject_current();
                    dispatcher.status |= STATUS_UART_ERROR;
                    error_present = true;
                    break;
                }
            };
            let result = match event {
                // Ordinary bytes are assembled into a COBS frame outside ISR
                // context, where decoding time cannot lengthen interrupt latency.
                RxEvent::Byte { value, .. } => receiver.push(value),
                RxEvent::Error { .. } => {
                    // A hardware receive error invalidates the entire partial
                    // packet rather than only the byte carrying the flag.
                    receiver.reject_current();
                    dispatcher.status |= STATUS_UART_ERROR;
                    error_present = true;
                    ReceiveResult::None
                }
            };
            match result {
                // Most bytes only advance the collector and require no action.
                ReceiveResult::None => {}
                ReceiveResult::Rejected => {
                    // Malformed COBS, length, version, type, or CRC is observable
                    // as a latched communication fault but receives no reply.
                    dispatcher.status |= STATUS_UART_ERROR;
                    error_present = true;
                }
                ReceiveResult::Frame(packet) => {
                    // Sample the physical button immediately before status
                    // dispatch so GET_STATUS returns a recent input value.
                    dispatcher.inputs = u8::from(button.is_low());

                    // The dispatcher can now trust framing and checksum and may
                    // update only logical state; main retains hardware ownership.
                    let outcome = dispatcher.handle(packet);
                    if outcome.outputs_changed {
                        // Convert the coherent logical mask to all active-low
                        // GPIO levels only after successful command validation.
                        outputs.apply_mask(dispatcher.outputs.mask());
                    }
                    if outcome.config_changed {
                        // Keep storage changes visibly faulted until the P1 EEPROM
                        // layer can commit and verify them atomically.
                        dispatcher.status |= STATUS_CONFIG_FAULT;
                        error_present = true;
                    }
                    if let Some(response) = outcome.response {
                        // Encode into bounded storage, then let Serial control DE,
                        // wait for TC, and release the half-duplex bus.
                        let len = protocol::encode(&response, &mut wire_response);
                        if serial
                            .write_response(&wire_response[..len], &timer)
                            .is_err()
                        {
                            // A failed response is a communication fault, but the
                            // foreground continues so watchdog policy remains live.
                            dispatcher.status |= STATUS_UART_ERROR;
                            error_present = true;
                        }
                    }
                    if outcome.stop_watchdog_feed {
                        // This flag is set only after write_response returned, so
                        // the reset acknowledgement has completely left the UART.
                        watchdog_feed_enabled = false;
                    }
                    // Any valid addressed command produces short blue activity
                    // feedback, including commands with no state change.
                    command_flash_started = Some(timer.now_ticks());
                }
            }
        }
        if watchdog_feed_enabled {
            // Keep this as the final, lowest-priority operation. Moving it above
            // receive, dispatch, GPIO, LED, or TX work would hide a stalled loop.
            watchdog.feed();
        }
    }
}
