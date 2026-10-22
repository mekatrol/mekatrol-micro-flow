# Dependency provenance

## RP2040 second-stage boot

The project-owned `soc/rp2040/boot2_generic_03h.S` is derived from Raspberry
Pi's `boot2_generic_03h.S` in pico-sdk 2.2.0, licensed BSD-3-Clause.

- Source: <https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/boot_stage2/boot2_generic_03h.S>
- Pinned version: pico-sdk 2.2.0
- Modifications: SDK macros and includes were replaced by their RP2040 register
  constants; the position-independent boot and vector handoff are retained.
- Update procedure: compare the pinned upstream file and RP2040 datasheet,
  update the local constants deliberately, then rebuild and test cold boot on
  the RP2040-Zero. `tools/pad_boot2.py` pads the result and calculates the
  checksum expected by the RP2040 boot ROM.

No Espressif source or binary is linked into the ESP32 application images.
Espressif GCC 15.2.0_20251204 and esptool 5.1.0 are host-side build tools only.
They are the pinned versions for the initial targets. Update both board images,
inspect their load segments, and repeat cold-boot hardware tests before changing
either version. Arm GNU Toolchain 13.2.Rel1 is pinned for the RP2040 build by
the same policy. Installation and activation instructions are maintained in
[PROJECT_SETUP.md](../PROJECT_SETUP.md).
