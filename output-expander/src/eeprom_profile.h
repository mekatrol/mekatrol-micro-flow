#ifndef OUTPUT_EXPANDER_EEPROM_PROFILE_H
#define OUTPUT_EXPANDER_EEPROM_PROFILE_H

#include <stdint.h>

/*
 * Compile-time EEPROM selection.
 *
 * WHAT:
 * Select the physical serial EEPROM fitted to this PCB build.  The rest of
 * the firmware deliberately does not contain either part number.
 *
 * WHY:
 * The development AT24C256 and production XBLW 24C02S use the same I2C
 * commands, but they do not encode the internal byte address in the same
 * number of bytes and they wrap page writes at different boundaries.  Mixing
 * either value can silently write the wrong location, so these properties are
 * kept together as one reviewed device profile.
 *
 * Production builds select the small device with:
 *   cmake ... -DOUTPUT_EXPANDER_EEPROM_PROFILE=2
 * Development defaults to the presently fitted AT24C256.
 */
#define EEPROM_DEVICE_AT24C256 1
#define EEPROM_DEVICE_XBLW_24C02S 2

#ifndef OUTPUT_EXPANDER_EEPROM
#define OUTPUT_EXPANDER_EEPROM EEPROM_DEVICE_AT24C256
#endif

/* Both boards strap/use all hardware address bits as zero: 1010 000b = 0x50. */
#define EEPROM_I2C_ADDRESS_7BIT UINT8_C(0x50)

#if OUTPUT_EXPANDER_EEPROM == EEPROM_DEVICE_AT24C256
#define EEPROM_CAPACITY_BYTES UINT32_C(32768)
#define EEPROM_PAGE_SIZE_BYTES UINT16_C(64)
#define EEPROM_WORD_ADDRESS_BYTES 2U
#define EEPROM_PROFILE_NAME "AT24C256"
#elif OUTPUT_EXPANDER_EEPROM == EEPROM_DEVICE_XBLW_24C02S
#define EEPROM_CAPACITY_BYTES UINT32_C(256)
#define EEPROM_PAGE_SIZE_BYTES UINT16_C(16)
#define EEPROM_WORD_ADDRESS_BYTES 1U
#define EEPROM_PROFILE_NAME "XBLW 24C02S"
#else
#error "Unknown OUTPUT_EXPANDER_EEPROM selection"
#endif

/* The common configuration layout occupies 0x00..0x7f on both devices. */
_Static_assert(EEPROM_CAPACITY_BYTES >= UINT32_C(128),
               "Selected EEPROM is too small for two configuration slots");

#endif
