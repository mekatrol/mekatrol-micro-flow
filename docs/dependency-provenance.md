# Dependency Provenance

## Raspberry Pi RP2040 second-stage boot

| Field | Value |
|---|---|
| Artifact | `src/boards/rpi/rp2040/boot2_generic_03h.S` |
| Upstream | Raspberry Pi `pico-sdk` |
| Recorded upstream version | 2.2.0 |
| Licence | BSD-3-Clause |
| Use | RP2040 external-flash second-stage boot |
| Local handling | Built and padded to 256 bytes by project image tools |

Update by importing the corresponding upstream source, reviewing local
integration, then rebuilding Pico and running boot, size, and hardware
conformance checks.

No vendor SDK is currently linked into the portable library or the remaining
Pico 2 firmware targets.

## Raspberry Pi Pico SDK and TinyUSB

| Field | Value |
|---|---|
| Dependency | Raspberry Pi Pico SDK, including bundled TinyUSB |
| Version | 2.3.1 |
| Upstream | `https://github.com/raspberrypi/pico-sdk` |
| Boundary | Raspberry Pi Pico RP2040 firmware only |
| Enabled use | Startup, clocks, GPIO, IRQs, timer, UART, watchdog, and USB CDC |
| Local modifications | None |

Update the pinned SDK checkout and its TinyUSB submodule together, then run all
host tests, build the Pico firmware, compare size output, and repeat Pico USB,
GPIO, reset, UART, watchdog, and supervisor hardware validation.
