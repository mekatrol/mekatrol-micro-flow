# Dependency provenance

## Native SDK policy

The portable library under `src/lib` does not link vendor SDKs or expose their
types. Firmware under `src/boards` may link pinned native SDK components and
maintained middleware for startup, SoC services, peripheral drivers, and
protocol stacks. Each target records the exact version, licence, enabled
components, configuration, modifications, update procedure, size impact, and
hardware verification evidence here.

The Waveshare RP2040 Zero Stage 3 implementation uses Raspberry Pi Pico SDK
2.3.1 and the TinyUSB revision bundled as its `lib/tinyusb` submodule.

- Source and version: <https://github.com/raspberrypi/pico-sdk/tree/2.3.1>;
  TinyUSB is resolved by that tag's submodule commit.
- Licence: retain the Pico SDK and TinyUSB licence files from their installed
  source trees; both are permissively licensed.
- Linked interfaces: `pico_stdlib`, `hardware_gpio`, `hardware_irq`,
  `hardware_sync`, `hardware_timer`, `hardware_uart`, `hardware_watchdog`,
  `tinyusb_device`, and `tinyusb_board`.
- Configuration: one full-speed CDC ACM interface, 64-byte endpoint packets,
  128-byte TinyUSB receive FIFO, and 256-byte TinyUSB transmit FIFO. Micro Flow
  adds bounded 256-byte ingress and egress queues.
- Local modifications: none. Board-owned TinyUSB configuration and descriptors
  live under `src/boards/rpi/waveshare-rp2040-zero`.
- Update procedure: install the proposed SDK independently, update the CMake
  version check and this record, inspect the TinyUSB submodule change, rebuild
  all affected targets, compare ELF/map size, and repeat boot, enumeration,
  reconnect, bidirectional traffic, and watchdog tests on hardware.

## RP2040 second-stage boot

The project-owned
`src/boards/rpi/rp2040/boot2_generic_03h.S` is derived from Raspberry
Pi's `boot2_generic_03h.S` in pico-sdk 2.2.0, licensed BSD-3-Clause.

- Source: <https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/boot_stage2/boot2_generic_03h.S>
- Pinned version: pico-sdk 2.2.0
- Modifications: SDK macros and includes were replaced by their RP2040 register
  constants; the position-independent boot and vector handoff are retained.
- Update procedure: compare the pinned upstream file and RP2040 datasheet,
  update the local constants deliberately, then rebuild and test cold boot on
  the Raspberry Pi Pico and RP2040-Zero. `tools/pad_boot2.py` pads the result and calculates the
  checksum expected by the RP2040 boot ROM.

Current ESP32 application images do not yet link ESP-IDF components. Future
board integrations may select pinned ESP-IDF components under the native SDK
policy above. Espressif GCC 15.2.0_20251204, ESP-IDF 6.0.2, and esptool 5.1.0
are the initial pinned ESP32 tool and SDK versions. Update board images, inspect
their load segments, and repeat cold-boot hardware tests before changing them.
Arm GNU Toolchain 13.2.Rel1 and Pico SDK 2.3.1 are the corresponding initial
RP2040 versions. Installation and activation instructions are maintained in
[PROJECT_SETUP.md](../PROJECT_SETUP.md).
