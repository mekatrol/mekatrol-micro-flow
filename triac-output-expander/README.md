# Triac Output Expander

Firmware for the Mekatrol eight-channel triac output expansion board.

This crate is intentionally hardware-neutral until the microcontroller and
hardware abstraction layer are selected. It currently defines only the board's
logical eight-output state and provides a `no_std` foundation for embedded
firmware.

## Development

Run the host-side checks from this directory:

```sh
cargo test
cargo clippy --all-targets -- -D warnings
```

Add the target, linker configuration, HAL, entry point, and flashing runner
once the board MCU has been chosen.
