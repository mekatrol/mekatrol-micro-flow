# RP2040 SoC support

This folder owns RP2040 startup, linker layout, SoC support, and the
second-stage flash boot implementation used by Raspberry Pi Pico. It must not
leak target details into portable Micro Flow APIs. Physical resource mappings
remain in the board folder.
