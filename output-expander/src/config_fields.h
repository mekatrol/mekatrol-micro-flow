
#ifndef OUTPUT_EXPANDER_CONFIG_FIELDS_H
#define OUTPUT_EXPANDER_CONFIG_FIELDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Protocol v1 configuration field sizes.
 *
 * WHAT:
 * SET_CONFIG contains exactly five bytes:
 *   startup mask       : u8
 *   timeout            : u16 little-endian
 *   timeout output mask: u8
 *   LED brightness     : u8
 *
 * GET_CONFIG returns the commissioned device address
 * followed by the same five configuration bytes.
 *
 * WHY:
 * These are protocol payload sizes, not the persistent
 * EEPROM record size. EEPROM records additionally
 * contain metadata, versioning, sequence and CRC-32.
 */
#define CONFIG_FIELDS_PAYLOAD_SIZE 5U
#define CONFIG_FIELDS_GET_RESPONSE_SIZE 6U

/*
 * Portable configuration field representation.
 *
 * WHAT:
 * Store the four protocol configuration fields using
 * standard fixed-width integer types.
 *
 * WHY:
 * The C compiler may insert padding into structures.
 * This structure must never be written directly to
 * EEPROM or transmitted as raw memory.
 *
 * The encoding functions explicitly define byte order.
 *
 * An output bit value of 1 means logical ON. The GPIO
 * HAL handles conversion to the active-low pin level.
 *
 * Timeout zero disables communication timeout monitoring.
 * The timing implementation is outside this module.
 */
typedef struct {
  uint8_t startup_mask;
  uint16_t timeout_ms;
  uint8_t timeout_mask;
  uint8_t brightness;
} config_fields_t;

/*
 * Decode the exact five-byte SET_CONFIG payload.
 *
 * Returns false for missing pointers or invalid length.
 * On failure, the destination structure is unchanged.
 *
 * This function validates the representation only.
 * It does not commit settings or modify GPIO outputs.
 */
bool config_fields_decode(const uint8_t *payload, size_t length,
                          config_fields_t *fields);

/*
 * Serialize the exact five-byte configuration format.
 *
 * The caller must provide at least five output bytes.
 * Extra output capacity is permitted and left untouched.
 *
 * On invalid arguments, no output bytes are modified.
 */
bool config_fields_encode(const config_fields_t *fields, uint8_t *output,
                          size_t capacity);

/*
 * Serialize the six-byte GET_CONFIG response payload.
 *
 * The first byte is the commissioned address (0x01..0xFE).
 * The remaining five bytes contain the configuration.
 *
 * On failure, no output bytes are modified.
 *
 * This function does not construct a protocol header,
 * CRC or COBS frame. Those belong to existing modules.
 */
bool config_fields_encode_get(uint8_t address, const config_fields_t *fields,
                              uint8_t *output, size_t capacity);

/*
 * Compare all configuration fields for equality.
 *
 * WHY:
 * Avoid raw structure comparisons, since padding bytes
 * are not part of the configuration and may be undefined.
 *
 * This will also support avoiding unnecessary EEPROM
 * writes when a requested configuration is unchanged.
 */
bool config_fields_equal(const config_fields_t *a, const config_fields_t *b);

#endif
