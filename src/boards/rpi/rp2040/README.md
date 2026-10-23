# RP2040 SoC support

This folder owns RP2040 startup, linker layout, SoC support, and the
second-stage flash boot implementation shared by all RP2040 boards. It may use
pinned Raspberry Pi Pico SDK hardware components and maintained middleware such
as TinyUSB. Those dependencies remain private to RP2040/board firmware and must
not leak into portable Micro Flow APIs. Physical resource mappings remain in
each board folder.
