
#include "config_fields.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Verify the exact protocol byte order.
 *
 * WHAT:
 * Decode a known configuration payload and verify
 * every field, then encode it again.
 *
 * WHY:
 * A correct round-trip alone is insufficient: two
 * incorrectly implemented functions could agree with
 * each other while violating the protocol.
 *
 * This explicit expected vector checks interoperability.
 */
static void test_known_vector(void) {
  const uint8_t wire[CONFIG_FIELDS_PAYLOAD_SIZE] = {0x81U, 0x34U, 0x12U, 0x42U,
                                                    0xFFU};

  config_fields_t parsed = {0};

  assert(config_fields_decode(wire, sizeof(wire), &parsed));

  assert(parsed.startup_mask == 0x81U);
  assert(parsed.timeout_ms == 0x1234U);
  assert(parsed.timeout_mask == 0x42U);
  assert(parsed.brightness == 0xFFU);

  uint8_t output[CONFIG_FIELDS_PAYLOAD_SIZE] = {0};

  assert(config_fields_encode(&parsed, output, sizeof(output)));

  assert(memcmp(output, wire, sizeof(wire)) == 0);

  /*
   * GET_CONFIG prefixes the commissioned address.
   * Its remaining bytes match the SET_CONFIG fields.
   */
  uint8_t get[CONFIG_FIELDS_GET_RESPONSE_SIZE] = {0};

  const uint8_t expected_get[] = {0x23U, 0x81U, 0x34U, 0x12U, 0x42U, 0xFFU};

  assert(config_fields_encode_get(0x23U, &parsed, get, sizeof(get)));

  assert(memcmp(get, expected_get, sizeof(get)) == 0);
}

/*
 * Exhaustively test all 65,536 timeout values.
 *
 * WHAT:
 * Encode and decode every possible unsigned 16-bit
 * timeout, also varying the three byte-sized fields.
 *
 * WHY:
 * Detect byte swaps, lost high bits, signed conversion,
 * incorrect zero handling and truncation.
 *
 * Timeout zero means monitoring disabled.
 * Timeout 65535 is the largest representable value.
 *
 * Every possible value of each eight-bit field appears
 * during the loop.
 */
static void test_all_timeout_values(void) {
  for (unsigned int timeout = 0U; timeout <= 65535U; ++timeout) {

    config_fields_t input = {(uint8_t)timeout, (uint16_t)timeout,
                             (uint8_t)(timeout >> 8U),
                             (uint8_t)(255U - (timeout & 255U))};

    uint8_t wire[CONFIG_FIELDS_PAYLOAD_SIZE] = {0};
    config_fields_t decoded = {0};

    assert(config_fields_encode(&input, wire, sizeof(wire)));

    assert(config_fields_decode(wire, sizeof(wire), &decoded));

    assert(config_fields_equal(&input, &decoded));
  }
}

/*
 * Verify error behavior and unchanged output storage.
 *
 * WHAT:
 * Test NULL arguments, truncated capacities, invalid
 * payload lengths and reserved addresses.
 *
 * WHY:
 * Invalid command handling must never publish a
 * partial configuration or response payload.
 *
 * The 0xA5 sentinel exposes accidental output writes.
 */
static void test_invalid_arguments(void) {
  const config_fields_t original = {0xA5U, 0x1234U, 0x5AU, 0x55U};

  const uint8_t payload[CONFIG_FIELDS_PAYLOAD_SIZE] = {0};

  config_fields_t decoded = original;

  uint8_t output[CONFIG_FIELDS_GET_RESPONSE_SIZE];
  memset(output, 0xA5, sizeof(output));

  assert(!config_fields_decode(NULL, sizeof(payload), &decoded));

  assert(!config_fields_decode(payload, sizeof(payload), NULL));

  /*
   * The decoder checks exact length before any data
   * access. Even an oversized declared length is
   * rejected safely without reading beyond the
   * provided five-byte test buffer.
   */
  for (size_t n = 0U; n <= 255U; ++n) {
    if (n != CONFIG_FIELDS_PAYLOAD_SIZE) {
      assert(!config_fields_decode(payload, n, &decoded));
    }
  }

  assert(config_fields_equal(&original, &decoded));

  assert(!config_fields_encode(NULL, output, sizeof(output)));

  assert(!config_fields_encode(&original, NULL, sizeof(output)));

  assert(!config_fields_encode(&original, output, 4U));

  assert(!config_fields_encode_get(0x00U, &original, output, sizeof(output)));

  assert(!config_fields_encode_get(0xFFU, &original, output, sizeof(output)));

  assert(!config_fields_encode_get(0x23U, NULL, output, sizeof(output)));

  assert(!config_fields_encode_get(0x23U, &original, NULL, sizeof(output)));

  assert(!config_fields_encode_get(0x23U, &original, output, 5U));

  /*
   * No failed operation may alter the output buffer.
   */
  for (size_t i = 0U; i < sizeof(output); ++i) {
    assert(output[i] == 0xA5U);
  }

  assert(!config_fields_equal(NULL, &original));
  assert(!config_fields_equal(&original, NULL));
}

/*
 * Exercise every commissioned device address.
 *
 * WHAT:
 * Verify that GET_CONFIG serialization accepts
 * 0x01..0xFE and preserves the address exactly.
 *
 * WHY:
 * No valid device address may be unintentionally
 * excluded by the response serializer.
 */
static void test_commissioned_addresses(void) {
  const config_fields_t fields = {0U, 0U, 0U, 0U};

  for (unsigned int address = 1U; address <= 254U; ++address) {
    uint8_t output[CONFIG_FIELDS_GET_RESPONSE_SIZE] = {0};

    assert(config_fields_encode_get((uint8_t)address, &fields, output,
                                    sizeof(output)));

    assert(output[0] == (uint8_t)address);

    for (size_t i = 1U; i < sizeof(output); ++i) {
      assert(output[i] == 0U);
    }
  }
}

/*
 * Verify field-based semantic equality.
 *
 * WHY:
 * Equality is later used to identify unchanged
 * persistent configuration and avoid unnecessary
 * EEPROM wear.
 *
 * The result must depend only on defined fields.
 */
static void test_equality(void) {
  const config_fields_t a = {0xFFU, 65535U, 0x80U, 0x00U};

  config_fields_t b = a;

  assert(config_fields_equal(&a, &b));

  b.brightness = 1U;
  assert(!config_fields_equal(&a, &b));

  b = a;
  b.timeout_ms = 0U;
  assert(!config_fields_equal(&a, &b));
}

/*
 * Run the complete portable configuration codec tests.
 *
 * No hardware registers, output driver, EEPROM,
 * interrupts or USART support are required.
 */
int main(void) {
  test_known_vector();
  test_all_timeout_values();
  test_invalid_arguments();
  test_commissioned_addresses();
  test_equality();

  return 0;
}
