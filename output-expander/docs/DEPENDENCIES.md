# Phase 0 dependency inventory

| Input | Pin and provenance | Licence / ownership | Update rule |
|---|---|---|---|
| Compiler and binutils | xPack GNU RISC-V Embedded GCC `14.3.0-1`, official portable release | GPL and bundled upstream component licences | Change the pin only with clean builds, disassembly/ABI review, and hardware validation |
| Register definitions | Project-owned narrow definitions in `src/ch32v003.c`, checked against WCH CH32V003 RM v1.7 and the existing board firmware | Project licence | Review every added register against the pinned manual |
| Startup | Project-owned `startup.S`; informed by QingKe runtime conventions but independently implemented | Project licence | Audit reset disassembly and initialization on every change |
| Linker script | Project-owned `ch32v003.ld`; 16 KiB Flash and 2 KiB SRAM from CH32V003F4P6 documentation | Project licence | Recheck ELF layout and physical part before changing |
| Programmer | `wlink 0.1.3`, invoked explicitly for `CH32V003` at low (400 kHz) SWIO speed; <https://github.com/ch32-rs/wlink> | MIT/Apache-2.0 | Pin a new version only after attach/flash/reset/recovery testing |
| Hardware documents | WCH `CH32V003DS0.pdf` and `CH32V003RM.pdf`, retained in `docs/` | Vendor documentation | Record document revision and re-audit affected definitions |

No vendor framework, downloaded header, runtime library, libc, or compiler
runtime is linked into the Phase 0 image. The firmware is freestanding and
uses only project-owned C, assembly, and linker inputs.
