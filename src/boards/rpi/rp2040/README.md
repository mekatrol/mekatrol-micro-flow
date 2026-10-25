# RP2040 SoC support

This folder owns the Pico SDK-backed RP2040 and TinyUSB CDC adapters used by
the Raspberry Pi Pico and Waveshare RP2040-Zero firmware variants. Startup,
linking, and FreeRTOS SMP integration are supplied by the pinned Pico SDK and
FreeRTOS kernel. SDK and RTOS types must not leak into portable Micro Flow APIs;
physical resource mappings remain in the board folder.
