
#include "protocol.h"

#include "cobs.h"
#include "crc16.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Encode and decode a PING request.
 *
 * WHAT:
 * Verify the header fields, zero-length payload,
 * terminating delimiter and packet round trip.
 *
 * WHY:
 * This is the smallest valid protocol packet
 * and will later be used for device discovery.
 */
static void test_ping_round_trip(void) {
  const protocol_packet_t request = {.destination = 0x01U,
                                     .sequence = 0x42U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = 0x01U,
                                     .payload_length = 0U};

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t wire_length = 0U;

  assert(protocol_encode(&request, wire, sizeof(wire), &wire_length) ==
         PROTOCOL_OK);

  assert(wire_length >= 2U);
  assert(wire[wire_length - 1U] == 0U);

  /*
   * Only the final delimiter may be zero.
   */
  for (size_t i = 0U; i < wire_length - 1U; ++i) {
    assert(wire[i] != 0U);
  }

  protocol_packet_t decoded = {0};

  assert(protocol_decode(wire, wire_length - 1U, &decoded) == PROTOCOL_OK);

  assert(decoded.destination == request.destination);
  assert(decoded.sequence == request.sequence);
  assert(decoded.type == request.type);
  assert(decoded.command == request.command);
  assert(decoded.payload_length == 0U);
}

/*
 * Verify the protocol's maximum permitted payload.
 *
 * The complete decoded packet is exactly 32 bytes.
 * The encoded wire frame cannot exceed 34 bytes.
 */
static void test_maximum_payload(void) {
  protocol_packet_t request = {.destination = 0xFEU,
                               .sequence = 0xFFU,
                               .type = PROTOCOL_TYPE_RESPONSE,
                               .command = 0x20U,
                               .payload_length = PROTOCOL_MAX_PAYLOAD};

  for (size_t i = 0U; i < PROTOCOL_MAX_PAYLOAD; ++i) {
    request.payload[i] = (uint8_t)(i * 7U);
  }

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t length = 0U;

  assert(protocol_encode(&request, wire, sizeof(wire), &length) == PROTOCOL_OK);

  assert(length <= PROTOCOL_MAX_ENCODED_SIZE);

  protocol_packet_t decoded = {0};

  assert(protocol_decode(wire, length - 1U, &decoded) == PROTOCOL_OK);

  assert(decoded.payload_length == PROTOCOL_MAX_PAYLOAD);

  assert(memcmp(decoded.payload, request.payload, PROTOCOL_MAX_PAYLOAD) == 0);
}

/*
 * A corrupted CRC must reject the complete packet.
 *
 * COBS is valid, so this checks the CRC error path
 * independently of COBS structural validation.
 */
static void test_bad_crc(void) {
  const protocol_packet_t request = {.destination = 0x01U,
                                     .sequence = 0x02U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = 0x11U,
                                     .payload_length = 0U};

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t length = 0U;

  assert(protocol_encode(&request, wire, sizeof(wire), &length) == PROTOCOL_OK);

  uint8_t raw[PROTOCOL_MAX_DECODED_SIZE];
  size_t raw_length = 0U;

  assert(cobs_decode(wire, length - 1U, raw, sizeof(raw), &raw_length));

  /*
   * Change CMD without updating the checksum.
   * Re-encode to keep the COBS structure valid.
   */
  raw[4] ^= 0x01U;

  size_t body_length = 0U;

  assert(cobs_encode(raw, raw_length, wire, sizeof(wire), &body_length));

  protocol_packet_t decoded = {0};

  assert(protocol_decode(wire, body_length, &decoded) == PROTOCOL_ERROR_CRC);
}

/*
 * Construct a packet with a deliberately modified
 * field and recompute its CRC.
 *
 * WHY:
 * Without recomputing the checksum, a test of
 * invalid VERSION or TYPE might only test
 * CRC rejection rather than the intended field.
 */
static protocol_result_t decode_modified_field(size_t field_offset,
                                               uint8_t replacement) {
  uint8_t raw[8] = {0x01U, 0x01U, 0x02U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U};

  raw[field_offset] = replacement;

  const uint16_t crc = crc16_ccitt_false(raw, 6U);

  raw[6] = (uint8_t)(crc & UINT16_C(0xFF));
  raw[7] = (uint8_t)(crc >> 8);

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t body_length = 0U;

  assert(cobs_encode(raw, sizeof(raw), wire, sizeof(wire), &body_length));

  protocol_packet_t decoded = {0};

  return protocol_decode(wire, body_length, &decoded);
}

static void test_invalid_fields(void) {
  assert(decode_modified_field(0U, 2U) == PROTOCOL_ERROR_VERSION);

  assert(decode_modified_field(3U, 3U) == PROTOCOL_ERROR_TYPE);

  /*
   * Declaring one payload byte while sending
   * none must fail the exact-length check.
   */
  assert(decode_modified_field(5U, 1U) == PROTOCOL_ERROR_LENGTH);

  assert(decode_modified_field(5U, 25U) == PROTOCOL_ERROR_LENGTH);
}

static void test_invalid_cobs(void) {
  const uint8_t invalid[] = {0x05U, 0x11U, 0x22U};

  protocol_packet_t packet = {0};

  assert(protocol_decode(invalid, sizeof(invalid), &packet) ==
         PROTOCOL_ERROR_COBS);
}

static void test_small_output_buffer(void) {
  const protocol_packet_t request = {.destination = 1U,
                                     .sequence = 1U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = 1U,
                                     .payload_length = 0U};

  uint8_t output[2];
  size_t length = 99U;

  assert(protocol_encode(&request, output, sizeof(output), &length) ==
         PROTOCOL_ERROR_BUFFER);

  assert(length == 0U);
}

/*
 * An invalid frame must not overwrite a previously
 * accepted packet. The dispatcher must never act on
 * partially decoded or corrupted packet contents.
 */
static void test_error_preserves_packet(void) {
  protocol_packet_t packet = {.destination = 0x57U, .sequence = 0x83U};

  const uint8_t invalid[] = {0x00U};

  assert(protocol_decode(invalid, sizeof(invalid), &packet) ==
         PROTOCOL_ERROR_COBS);

  assert(packet.destination == 0x57U);
  assert(packet.sequence == 0x83U);
}

int main(void) {
  test_ping_round_trip();
  test_maximum_payload();
  test_bad_crc();
  test_invalid_fields();
  test_invalid_cobs();
  test_small_output_buffer();
  test_error_preserves_packet();

  puts("All protocol packet tests passed.");
  return 0;
}
