# Raspberry Pi Pico and Waveshare RP2040-Zero

Partial RP2040 firmware target using Pico SDK 2.3.1, FreeRTOS Kernel V11.3.0,
and TinyUSB. It runs statically allocated USB and status tasks under the RP2040
SMP port and provides a USB CDC management console. Flow execution, uploads,
application GPIO, and watchdog supervision are not enabled yet. The target
produces ELF, map, BIN, and UF2 files.

The `pico-debug` preset uses the Raspberry Pi Pico mapping and drives its
digital status LED on GPIO25. The `waveshare-rp2040-zero-debug` preset defines
`MICROFLOW_WAVESHARE_RP2040_ZERO` and drives the Waveshare board's WS2812 RGB
status LED on GPIO16. Selecting GPIO16 alone is insufficient because the
WS2812 requires its serial waveform rather than a steady GPIO level.

Configure before invoking a build preset:

```sh
cmake --preset waveshare-rp2040-zero-debug
cmake --build --preset waveshare-rp2040-zero-debug
```

The resulting Waveshare image is
`build/waveshare-rp2040-zero-debug/microflow-raspberry-pi-pico.uf2`.

Compilation and host regression tests do not establish USB enumeration,
FreeRTOS scheduling behavior, electrical status output, or hardware
conformance; those remain physical-board validation activities.
