
#include "cobs.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Verify the example from the protocol
 * specification.
 *
 * Raw:
 *   11 22 00 33
 *
 * Encoded:
 *   03 11 22 02 33
 */
static void test_reference_vector(void) {
  const uint8_t input[] = {0x11U, 0x22U, 0x00U, 0x33U};

  const uint8_t expected[] = {0x03U, 0x11U, 0x22U, 0x02U, 0x33U};

  uint8_t encoded[COBS_MAX_ENCODED_SIZE];
  size_t length = 0U;

  assert(cobs_encode(input, sizeof(input), encoded, sizeof(encoded), &length));

  assert(length == sizeof(expected));
  assert(memcmp(encoded, expected, length) == 0);
}

/*
 * All-zero packets stress code-byte boundaries
 * because each original zero ends a run.
 */
static void test_zero_bytes(void) {
  const uint8_t input[] = {0x00U, 0x00U, 0x00U};

  const uint8_t expected[] = {0x01U, 0x01U, 0x01U, 0x01U};

  uint8_t encoded[COBS_MAX_ENCODED_SIZE];
  size_t length = 0U;

  assert(cobs_encode(input, sizeof(input), encoded, sizeof(encoded), &length));

  assert(length == sizeof(expected));
  assert(memcmp(encoded, expected, length) == 0);
}

/*
 * An empty input is a valid COBS payload.
 * Its encoded representation is one 0x01 byte.
 *
 * The later protocol packet decoder will reject
 * it because a valid packet needs a header and CRC.
 */
static void test_empty_input(void) {
  uint8_t encoded[COBS_MAX_ENCODED_SIZE];
  uint8_t decoded[COBS_MAX_DECODED_SIZE];
  size_t length = 0U;

  assert(cobs_encode(NULL, 0U, encoded, sizeof(encoded), &length));

  assert(length == 1U);
  assert(encoded[0] == 0x01U);

  assert(cobs_decode(encoded, length, decoded, sizeof(decoded), &length));

  assert(length == 0U);
}

/*
 * Test every permitted decoded packet length.
 *
 * Use varying byte values, including zero,
 * to exercise code-block boundaries.
 */
static void test_round_trip(void) {
  uint8_t input[COBS_MAX_DECODED_SIZE];
  uint8_t encoded[COBS_MAX_ENCODED_SIZE];
  uint8_t decoded[COBS_MAX_DECODED_SIZE];

  for (size_t length = 0U; length <= COBS_MAX_DECODED_SIZE; ++length) {

    for (size_t i = 0U; i < length; ++i) {
      input[i] = (uint8_t)((i * 17U) & 0xFFU);
    }

    size_t encoded_length = 0U;
    size_t decoded_length = 0U;

    assert(
        cobs_encode(input, length, encoded, sizeof(encoded), &encoded_length));

    assert(encoded_length <= COBS_MAX_ENCODED_SIZE);

    /*
     * COBS body must contain no delimiter.
     */
    for (size_t i = 0U; i < encoded_length; ++i) {
      assert(encoded[i] != 0U);
    }

    assert(cobs_decode(encoded, encoded_length, decoded, sizeof(decoded),
                       &decoded_length));

    assert(decoded_length == length);
    assert(memcmp(input, decoded, length) == 0);
  }
}

static void test_maximum_length(void) {
  uint8_t input[COBS_MAX_DECODED_SIZE];
  uint8_t encoded[COBS_MAX_ENCODED_SIZE];

  /*
   * All nonzero bytes create one continuous
   * run and require one additional COBS code.
   */
  memset(input, 0x55, sizeof(input));

  size_t length = 0U;

  assert(cobs_encode(input, sizeof(input), encoded, sizeof(encoded), &length));

  assert(length == 33U);
  assert(encoded[0] == 33U);
}

static void test_malformed_input(void) {
  uint8_t decoded[COBS_MAX_DECODED_SIZE];
  size_t length = 0U;

  /*
   * Code 0 is invalid in a COBS body.
   */
  const uint8_t invalid_zero[] = {0x00U};

  assert(!cobs_decode(invalid_zero, sizeof(invalid_zero), decoded,
                      sizeof(decoded), &length));

  /*
   * Code 5 requires four bytes, but only
   * two following bytes are available.
   */
  const uint8_t invalid_length[] = {0x05U, 0x11U, 0x22U};

  assert(!cobs_decode(invalid_length, sizeof(invalid_length), decoded,
                      sizeof(decoded), &length));

  /*
   * A literal zero cannot appear inside
   * the encoded COBS body.
   */
  const uint8_t invalid_embedded_zero[] = {0x03U, 0x11U, 0x00U};

  assert(!cobs_decode(invalid_embedded_zero, sizeof(invalid_embedded_zero),
                      decoded, sizeof(decoded), &length));
}

static void test_small_buffers(void) {
  uint8_t input[] = {0x11U, 0x22U, 0x33U};

  uint8_t small[2];
  size_t length = 0U;

  /*
   * The encoder must reject a destination
   * too small for the COBS representation.
   */
  assert(!cobs_encode(input, sizeof(input), small, sizeof(small), &length));

  assert(length == 0U);

  /*
   * The decoder must also reject a destination
   * that cannot hold the restored packet.
   */
  const uint8_t encoded[] = {0x04U, 0x11U, 0x22U, 0x33U};

  assert(!cobs_decode(encoded, sizeof(encoded), small, sizeof(small), &length));

  assert(length == 0U);
}

int main(void) {
  test_reference_vector();
  test_zero_bytes();
  test_empty_input();
  test_round_trip();
  test_maximum_length();
  test_malformed_input();
  test_small_buffers();

  puts("All COBS tests passed.");
  return 0;
}
