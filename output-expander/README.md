# Output Expander

Firmware for the Mekatrol eight-channel output expansion boards.

The boards use the WCH CH32V003 RISC-V microcontroller and are available with
two output-stage variants:

- Triac outputs for switching AC loads.
- FET outputs for switching DC loads.

The crate currently defines the logical eight-output state and provides a
`no_std` foundation shared by both variants. The CH32V003 HAL, linker
configuration, entry point, flashing runner, and variant-specific drivers will
be added with the first hardware implementation.

## Development

Run the host-side checks from this directory:

```sh
cargo test
cargo clippy --all-targets -- -D warnings
```

The intended embedded Rust compilation target is
`riscv32ec-unknown-none-elf`. Host-side checks intentionally remain available
without requiring the embedded toolchain.
