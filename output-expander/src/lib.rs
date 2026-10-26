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
/// Hardware-specific RS-485 transceiver direction control.
///
/// Supports both the production PCB (shared DE and /RE)
/// and evaluation board (independent DE and /RE).
pub mod rs485;
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
