# RP2040 SoC support

This folder owns the RP2040-specific Pico SDK and TinyUSB CDC adapters used by
the Raspberry Pi Pico and Waveshare RP2040-Zero firmware variants. RP2-family
drivers intended for reuse by RP2350, including the WS2812 PIO driver, live in
the adjacent `common` folder. Startup, linking, and FreeRTOS SMP integration
are supplied by the pinned Pico SDK and FreeRTOS kernel. SDK and RTOS types
must not leak into portable Micro Flow APIs; physical resource mappings remain
in the board folder.
