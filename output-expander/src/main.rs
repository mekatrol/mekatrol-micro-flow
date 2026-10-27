//! Interrupt-driven Modbus RTU firmware for the CH32V003.
//!
//! USART1 interrupts capture received bytes and hardware timestamps.
//! The foreground loop reconstructs RTU frames using t1.5/t3.5
//! silence intervals and passes completed requests to the server.
//!
//! UART transmission remains blocking, with RS-485 direction
//! controlled by the Serial driver.

#![no_main]
#![no_std]

// Hardware abstraction layer for the CH32V003.
use ch32_hal as hal;

// GPIO types needed to initialize RS-485 direction pins.
use hal::gpio::{Level, Output};

use output_expander::config::Config as DeviceConfig;

// Maximum response size supported by the Modbus server.
use output_expander::modbus::MAX_RESPONSE;

use output_expander::output_gpio::OutputGpios;

// Hardware abstraction for RS-485 direction control.
use output_expander::rs485::Rs485Direction;

// Serial driver encapsulating UART communication and RS-485 control.
use output_expander::serial::Serial;

// Modbus server responsible for interpreting complete requests.
use output_expander::server::Server;

// Maximum supported Modbus RTU request size.
use output_expander::uart::RX_CAPACITY;

// Foreground processing and synchronized handoff of UART events.
use output_expander::irq_consumer;

// Modbus RTU receiver using hardware-timestamped bytes.
use output_expander::rtu_receiver::RtuReceiver;

// Device silicon unique identifier.
use output_expander::uid;

// Stop execution if an unrecoverable panic occurs.
use panic_halt as _;

use output_expander::irq_rx::{self, RxEvent};
use output_expander::monotonic::current_ticks;

/// USART1 receive interrupt handler.
///
/// Invoked when the UART receives data or reports an enabled
/// receive error. The completed Modbus request will be processed
/// by the main loop, not inside this interrupt.
#[hal::interrupt]
unsafe fn USART1() {
    let uart = hal::pac::USART1;

    // Read status first.
    let status = uart.statr().read();

    let has_error = status.pe() || status.fe() || status.ne() || status.ore();

    if !status.rxne() && !has_error {
        return;
    }

    // Capture the timestamp before clearing RXNE.
    let ticks = current_ticks();

    // Reading DATAR after STATR consumes the received byte
    // and clears the associated receive/error conditions.
    let value = uart.datar().read().dr() as u8;

    if has_error {
        // The received byte is unreliable when a UART error is set.
        // Preserve the error's interrupt timestamp for RTU recovery.
        irq_rx::push_from_interrupt(RxEvent::Error { ticks });
    } else {
        // Preserve the byte and its original arrival timestamp.
        irq_rx::push_from_interrupt(RxEvent::Byte { value, ticks });
    }
}

/// Enable USART1 receive and receive-error interrupts.
///
/// USART1 has already been configured by Serial::new().
/// The foreground application must no longer call blocking_read()
/// after this function enables interrupt-driven reception.
///
/// # Safety
///
/// The USART1 interrupt handler and global receive queue must be
/// initialized before enabling the interrupt.
unsafe fn enable_usart1_rx_interrupts() {
    use hal::interrupt::InterruptExt;

    let uart = hal::pac::USART1;

    // Enable an interrupt whenever a received byte is available.
    uart.ctlr1().modify(|w| {
        w.set_rxneie(true);

        // Report parity errors separately from valid received bytes.
        w.set_peie(true);
    });

    // Enable framing, noise, and overrun error interrupts.
    uart.ctlr3().modify(|w| {
        w.set_eie(true);
    });

    // SAFETY:
    // USART1 is configured and its interrupt handler is installed.
    unsafe {
        hal::interrupt::USART1.enable();
    }
}

#[qingke_rt::entry]
fn main() -> ! {
    // Initialize the microcontroller hardware abstraction layer.
    //
    // This configures clocks and returns ownership of the
    // available hardware peripherals.
    let p = hal::init(hal::Config::default());

    // Configure the status LED for USART1 interrupt diagnostics.
    //
    // Keep the pin configured as an output for the entire test.
    // The interrupt handler will change its output state directly.
    let _status_led = Output::new(p.PC1, Level::High, Default::default());

    // Configure all output pins OFF before placing them under application
    // control. Channel order follows board::PROVISIONAL_OUTPUT_SIGNALS.
    // The current board is active-low, hence HIGH is the safe OFF level.
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

    // Initialize the free-running SysTick timer for Modbus RTU timing
    // and RS-485 turnaround delays.
    //
    // The timer will be used when the UART receive interrupt and
    // non-blocking timing logic are integrated.
    let timer = output_expander::monotonic::Monotonic::new();

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

    // Apply the configured startup mask only after every channel has first
    // reached the safe OFF level.
    outputs.apply_mask(server.bank.outputs.mask());

    // Frame timing uses the same SysTick tick rate as the USART interrupt.
    //
    // RtuReceiver owns the two receive buffers and detects Modbus RTU
    // frame boundaries using the t1.5 and t3.5 timing requirements.
    let mut receiver = RtuReceiver::new_with_tick_rate(timer.ticks_per_us());

    // Application-owned request buffer.
    //
    // A completed request is copied here before Modbus processing,
    // so Server::handle() does not need access to the receive buffers.
    let mut request = [0u8; RX_CAPACITY];

    // Fixed-size response buffer.
    let mut response = [0u8; MAX_RESPONSE];

    // Enable USART1 receive interrupts after the UART, Modbus server,
    // RTU receiver, and application buffers have been initialized.
    //
    // From this point onward, the USART1 interrupt handler is the
    // only code that consumes received bytes from the data register.
    unsafe {
        enable_usart1_rx_interrupts();
    }

    // ---------------------------------------------------------
    // Main application loop
    // ---------------------------------------------------------

    loop {
        // ---------------------------------------------------------
        // Process queued UART receive events
        // ---------------------------------------------------------
        //
        // USART1 timestamps incoming bytes in its interrupt handler.
        // The foreground receiver uses those timestamps to detect
        // RTU frame boundaries and reject malformed receive streams.
        irq_consumer::drain_global_events(&mut receiver);

        // ---------------------------------------------------------
        // Copy a completed Modbus request
        // ---------------------------------------------------------
        //
        // Check queue health and copy the completed request while
        // USART interrupts are excluded by a critical section.
        //
        // A None result means no request is currently available,
        // or another queued event needs processing.
        let Some(request_len) = irq_consumer::take_global_ready_frame(&mut receiver, &mut request)
        else {
            continue;
        };

        // ---------------------------------------------------------
        // Execute the Modbus request
        // ---------------------------------------------------------
        //
        // The request now belongs to the foreground application.
        // No receive-buffer borrow or critical section is held
        // while the server interprets it.
        let outcome = server.handle(&request[..request_len], &mut response);

        // Apply all eight logical output states as one bitmask update.
        if outcome.outputs_changed {
            outputs.apply_mask(server.bank.outputs.mask());
        }

        if outcome.config_changed {
            // TODO:
            // Persist server.bank.config to reserved flash.
        }

        // ---------------------------------------------------------
        // Transmit the Modbus response
        // ---------------------------------------------------------
        //
        // Broadcast requests and some commissioning operations
        // intentionally produce no response.
        if outcome.response_len == 0 {
            continue;
        }

        // The existing Serial driver controls RS-485 direction,
        // writes the response, waits for the final stop bit,
        // and returns the transceiver to receive mode.
        if serial
            .write_response(&response[..outcome.response_len], &timer)
            .is_err()
        {
            // TODO:
            // Record a UART transmission error in diagnostics.
        }
    }
}
