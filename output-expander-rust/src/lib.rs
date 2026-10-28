// `no_std` removes the desktop standard library because this firmware runs on
// a microcontroller without an operating system, filesystem, or normal heap.
#![no_std]

// Each public module owns one concern; exposing them also allows host-side
// unit tests and future firmware entry points to reuse the same logic.
/// Board-level constants and provisional signal mapping.
pub mod board;

/// Persistent configuration encoding, validation, and defaults.
pub mod config;

/// Hardware-independent command execution.
pub mod dispatcher;

/// Physical eight-channel GPIO output driver.
#[cfg(target_arch = "riscv32")]
pub mod output_gpio;

/// Compact logical representation of the eight output channels.
pub mod outputs;

/// Proprietary packet framing, COBS, and CRC-16/CCITT-FALSE.
pub mod protocol;

// RS-485 direction control and USART1 access require
// the CH32V003 hardware abstraction layer.
//
// These modules are compiled for firmware builds,
// but excluded from native host unit tests.
//
// This allows the protocol and receive-buffer tests
// to run on a development computer.
#[cfg(target_arch = "riscv32")]
pub mod rs485;

/// Hardware-specific USART1 and RS-485 communication driver.
///
/// Encapsulates UART reception, transmission, and bus direction.
#[cfg(target_arch = "riscv32")]
pub mod serial;

/// Interrupt-safe USART1 receive event queue.
///
/// Transfers UART events from the interrupt handler to the foreground parser.
pub mod irq_rx;
/// Free-running SysTick timer for hardware delays and application timing.
///
/// Provides microsecond-resolution timing without resetting the SysTick
/// counter, allowing application timing and RS-485 turnaround delays.
/// Available only on the RISC-V firmware target.
#[cfg(target_arch = "riscv32")]
pub mod monotonic;

/// Conversion from operating state to status-LED colours.
pub mod status_led;

/// Silicon unique-ID reading and register conversion.
pub mod uid;

/// USART1 configuration and interrupt handling.
#[cfg(target_arch = "riscv32")]
pub mod usart;

/// Progress checks that decide whether the watchdog may be fed.
pub mod watchdog;

/// GPIO waveform driver for the single PC1 WS2812 status LED.
#[cfg(target_arch = "riscv32")]
pub mod ws2812;
