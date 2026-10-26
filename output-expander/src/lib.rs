// `no_std` removes the desktop standard library because this firmware runs on
// a microcontroller without an operating system, filesystem, or normal heap.
#![no_std]

// Each public module owns one concern; exposing them also allows host-side
// unit tests and future firmware entry points to reuse the same logic.
/// Board-level constants and provisional signal mapping.
pub mod board;
/// Device discovery and address-assignment message handling.
pub mod commissioning;
/// Persistent configuration encoding, validation, and defaults.
pub mod config;
/// Modbus RTU parsing, checksums, and output command handling.
pub mod modbus;
/// Compact logical representation of the eight output channels.
pub mod outputs;
/// Mapping between application state and Modbus registers.
pub mod registers;
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

/// Hardware-independent RTU timing and completed-frame publication.
pub mod rtu_receiver;

/// Foreground processor for timestamped USART receive events.
///
/// Transfers queued UART bytes and errors into the Modbus RTU receiver,
/// allowing frame processing outside interrupt context.
pub mod irq_consumer;
/// Interrupt-safe USART1 receive event queue.
///
/// Transfers timestamped UART events from the interrupt handler
/// to the foreground Modbus RTU receiver.
pub mod irq_rx;
/// Free-running SysTick timer for Modbus RTU timing and hardware delays.
///
/// Provides microsecond-resolution timing without resetting the SysTick
/// counter, allowing accurate RTU frame detection and RS-485 turnaround
/// delays. Available only on the RISC-V firmware target.
#[cfg(target_arch = "riscv32")]
pub mod monotonic;
/// MODBUS message handling
pub mod server;
/// Conversion from operating state to status-LED colours.
pub mod status_led;
/// Modbus character-gap timing calculations.
pub mod timer;
/// UART receive buffering and half-duplex state definitions.
pub mod uart;
/// Silicon unique-ID reading and register conversion.
pub mod uid;
/// Progress checks that decide whether the watchdog may be fed.
pub mod watchdog;
