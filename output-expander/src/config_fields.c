
#include "config_fields.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed protocol offsets.
 *
 * WHAT:
 * Define the byte positions explicitly rather than
 * relying on C structure layout or MCU endianness.
 *
 * WHY:
 * Both the CH32V003 firmware and the host must produce
 * identical byte sequences for the same configuration.
 */
#define STARTUP_OFFSET 0U
#define TIMEOUT_LO_OFFSET 1U
#define TIMEOUT_HI_OFFSET 2U
#define TIMEOUT_MASK_OFFSET 3U
#define BRIGHTNESS_OFFSET 4U

bool config_fields_decode(const uint8_t *payload, size_t length,
                          config_fields_t *fields) {
  /*
   * Reject incorrect storage and length before reading.
   *
   * A truncated or oversized SET_CONFIG request must
   * not be accepted by ignoring missing or extra bytes.
   *
   * The caller is responsible for ensuring that the
   * supplied pointer refers to at least length bytes.
   */
  if (payload == NULL || fields == NULL ||
      length != CONFIG_FIELDS_PAYLOAD_SIZE) {
    return false;
  }

  /*
   * Construct a temporary configuration.
   *
   * WHY:
   * The caller must not observe a partial update.
   * All fields are read and assembled before publication.
   */
  config_fields_t candidate = {0};

  candidate.startup_mask = payload[STARTUP_OFFSET];

  /*
   * Protocol multi-byte fields are little-endian.
   *
   * The low-order byte contributes bits 0..7.
   * The high-order byte contributes bits 8..15.
   *
   * uint8_t operands are promoted to int by C integer
   * promotion rules. Explicit uint16_t conversions
   * make the intended 16-bit representation clear.
   *
   * The resulting value is always 0..65535.
   */
  candidate.timeout_ms =
      (uint16_t)((uint16_t)payload[TIMEOUT_LO_OFFSET] |
                 (uint16_t)((uint16_t)payload[TIMEOUT_HI_OFFSET] << 8U));

  candidate.timeout_mask = payload[TIMEOUT_MASK_OFFSET];
  candidate.brightness = payload[BRIGHTNESS_OFFSET];

  /*
   * Publish only after the entire payload was decoded.
   *
   * There is no GPIO operation or EEPROM write here.
   * Applying these values is a separate foreground
   * transaction after persistent storage verification.
   */
  *fields = candidate;

  return true;
}

bool config_fields_encode(const config_fields_t *fields, uint8_t *output,
                          size_t capacity) {
  /*
   * Validate before the first output write.
   *
   * On failure, the caller's buffer remains unchanged.
   * A larger buffer is permitted; only five bytes
   * are written.
   */
  if (fields == NULL || output == NULL ||
      capacity < CONFIG_FIELDS_PAYLOAD_SIZE) {
    return false;
  }

  /*
   * All possible eight-bit values are valid for the
   * output masks and brightness under protocol v1.
   *
   * Timeout 0 disables monitoring; nonzero values are
   * unsigned milliseconds.
   *
   * Physical active-low GPIO conversion does not
   * belong in the protocol codec.
   */
  output[STARTUP_OFFSET] = fields->startup_mask;

  /*
   * Explicitly serialize little-endian timeout bytes.
   *
   * These casts intentionally discard bits outside
   * the selected byte. No platform-native structure
   * layout or byte-order assumption is involved.
   */
  output[TIMEOUT_LO_OFFSET] = (uint8_t)(fields->timeout_ms & 0xFFU);

  output[TIMEOUT_HI_OFFSET] = (uint8_t)(fields->timeout_ms >> 8U);

  output[TIMEOUT_MASK_OFFSET] = fields->timeout_mask;
  output[BRIGHTNESS_OFFSET] = fields->brightness;

  return true;
}

bool config_fields_encode_get(uint8_t address, const config_fields_t *fields,
                              uint8_t *output, size_t capacity) {
  /*
   * GET_CONFIG is a unicast command.
   *
   * WHAT:
   * Reject reserved broadcast and uncommissioned
   * addresses before creating any response bytes.
   *
   * WHY:
   * Protocol v1 reserves those identities and does
   * not permit uncommissioned unicast responses.
   */
  if (fields == NULL || output == NULL ||
      capacity < CONFIG_FIELDS_GET_RESPONSE_SIZE || address == 0x00U ||
      address == 0xFFU) {
    return false;
  }

  /*
   * Publish the commissioned address first.
   *
   * All arguments have already been validated, and
   * the remaining buffer capacity is at least five
   * bytes, so the following encoding cannot fail.
   */
  output[0] = address;

  (void)config_fields_encode(fields, &output[1], capacity - 1U);

  return true;
}

bool config_fields_equal(const config_fields_t *a, const config_fields_t *b) {
  /*
   * NULL cannot represent a valid configuration.
   *
   * Compare semantic fields individually because
   * struct padding is not meaningful configuration
   * data and should not influence EEPROM wear policy.
   */
  if (a == NULL || b == NULL) {
    return false;
  }

  return a->startup_mask == b->startup_mask && a->timeout_ms == b->timeout_ms &&
         a->timeout_mask == b->timeout_mask && a->brightness == b->brightness;
}
