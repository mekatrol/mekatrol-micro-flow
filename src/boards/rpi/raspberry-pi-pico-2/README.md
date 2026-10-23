# Raspberry Pi Pico 2

Experimental RP2350 build targets are provided for the Cortex-M33 and Hazard3
RISC-V architectures. They currently produce compile/link smoke-test ELF, map,
and BIN files only. The binaries do not yet contain the RP2350 boot ROM image
definition and must not be flashed or described as bootable firmware.

The two targets share physical board ownership here while retaining separate
startup and linker files. GPIO, clock, timer, boot metadata, UF2 packaging, and
hardware conformance remain planned work.
