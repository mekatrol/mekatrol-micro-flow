# Raspberry Pi Pico

Partial RP2040 firmware target using Pico SDK 2.3.1 and TinyUSB. It provides the
bounded supervisor, UART0 upload on GPIO0/GPIO1, a native USB CDC management
console, logical GPIO resources, watchdog service, and the onboard GPIO25
status LED. It produces ELF, map, BIN, and UF2 files.

Compilation and host regression tests do not establish USB enumeration,
electrical GPIO behavior, or hardware conformance; those remain physical-board
validation activities.
