# Output Expander C firmware

This is the canonical CH32V003F4P6 C firmware. The preserved Rust reference
and recovery implementation is in `../output-expander-rust/`.

Because this product has one fixed MCU and board, production sources use a
flat `src/` directory. `main.c`, `ch32v003.c`, startup assembly, the linker
script, and portable Phase 0 smoke logic are kept together without generic
SoC/board/core abstraction layers.

The Phase 0 smoke image safely disables all outputs and RS-485 transmission,
then toggles active-low output 1 (PC0) every 500 ms. Connect an appropriately
current-limited LED/load to output 1 to observe a 1 Hz full cycle.

```sh
tools/clean.sh
tools/test.sh host-debug
tools/build.sh ch32v003-release
tools/flash.sh ch32v003-release
```

See [docs/BUILD.md](docs/BUILD.md) for prerequisites, wiring, artifacts, and
recovery instructions.
