# Phase 0 evidence — 2026-10-28

Baseline commit: `3b2c2420830aafc7a37ecf462a4249e884058cd0` (working tree path move performed without changing
Rust source behavior). Host: Linux x86-64. Board revision and physical smoke
result were not observable because no WCH-LinkE USB device was attached.

## Verified in this workspace

- Host compiler: GCC 13.3.0; CMake 4.2.1; Ninja 1.12.1.
- Cross compiler: official xPack `riscv-none-elf-gcc 14.3.0` package
  `14.3.0-1`, extracted temporarily for verification.
  Archive SHA-256: `be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726`.
- `tools/test.sh host-debug` passed with AddressSanitizer and UBSan.
- `tools/test.sh host-release` passed.
- Clean `ch32v003-debug` and `ch32v003-release` builds passed.
- Release: 424 bytes Flash, 4 bytes static SRAM; initial stack top
  `0x20000800`.
- ELF is ELF32 little-endian RISC-V, entry `0x00000000`, flags RVC/RVE,
  soft-float; architecture attribute is `rv32e2p0_c2p0_zicsr2p0_zca1p0`.
- Reset instruction at address zero jumps to `reset_handler`; disassembly shows
  only RV32E/C/Zicsr-compatible attributes and no interrupt ABI dependency.
- `wlink 0.1.3` is installed and the flash wrapper validates/builds the exact
  ELF before invoking it.
- Wrapper rejection of an unknown preset returns exit status 2.
- The moved Rust `cargo firmware --locked` release build passed independently.
  Its hardware-only `usart` module is now target-gated consistently with the
  other HAL modules, allowing the native host suite to run without changing
  firmware behavior.

## Physical completion record

Run the following with the target and WCH-LinkE attached, then append the board
revision, probe serial/identity, successful flash output, measured PC0 period,
and successful Rust recovery result:

```sh
tools/flash.sh ch32v003-release
cd ../output-expander-rust && cargo flash
```

The software side of the Phase 0 flash path is complete. The physical
flash/reset/heartbeat/recovery evidence remains deliberately unclaimed until
hardware is connected.
