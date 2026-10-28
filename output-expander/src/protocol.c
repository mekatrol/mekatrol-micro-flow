
#include "protocol.h"

#include "cobs.h"
#include "crc16.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Check that the packet and COBS modules agree on
 * their capacities.
 *
 * WHY:
 * A future change to either header must not
 * silently introduce a buffer-size mismatch.
 */
_Static_assert(PROTOCOL_MAX_DECODED_SIZE == PROTOCOL_HEADER_SIZE +
                                                PROTOCOL_MAX_PAYLOAD +
                                                PROTOCOL_CRC_SIZE,
               "Decoded packet capacity mismatch");

_Static_assert(PROTOCOL_MAX_DECODED_SIZE == COBS_MAX_DECODED_SIZE,
               "COBS decoded capacity mismatch");

_Static_assert(PROTOCOL_MAX_ENCODED_SIZE == COBS_MAX_FRAME_SIZE,
               "COBS wire capacity mismatch");

protocol_result_t protocol_encode(const protocol_packet_t *packet,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length) {
  if (output_length == NULL) {
    return PROTOCOL_ERROR_ARGUMENT;
  }

  /*
   * Never publish a length for a failed encode.
   * The caller must not transmit partial frames.
   */
  *output_length = 0U;

  if (packet == NULL || output == NULL) {
    return PROTOCOL_ERROR_ARGUMENT;
  }

  if (packet->payload_length > PROTOCOL_MAX_PAYLOAD) {
    return PROTOCOL_ERROR_LENGTH;
  }

  if (packet->type > PROTOCOL_TYPE_ERROR) {
    return PROTOCOL_ERROR_TYPE;
  }

  /*
   * Construct the unencoded packet in fixed
   * stack storage.
   *
   * Only the prefix defined by decoded_len
   * will be transmitted.
   */
  uint8_t decoded[PROTOCOL_MAX_DECODED_SIZE] = {0};

  const size_t payload_len = packet->payload_length;

  const size_t decoded_len =
      PROTOCOL_HEADER_SIZE + payload_len + PROTOCOL_CRC_SIZE;

  decoded[0] = PROTOCOL_VERSION;
  decoded[1] = packet->destination;
  decoded[2] = packet->sequence;
  decoded[3] = packet->type;
  decoded[4] = packet->command;
  decoded[5] = packet->payload_length;

  /*
   * Copy only valid payload bytes.
   *
   * Unused capacity in the packet structure
   * must never appear on the wire.
   */
  memcpy(&decoded[PROTOCOL_HEADER_SIZE], packet->payload, payload_len);

  /*
   * CRC-16 covers VERSION through the final
   * payload byte, excluding the CRC itself.
   *
   * Wire order is little-endian, so the
   * low checksum byte is stored first.
   */
  const uint16_t crc =
      crc16_ccitt_false(decoded, decoded_len - PROTOCOL_CRC_SIZE);

  decoded[decoded_len - 2U] = (uint8_t)(crc & UINT16_C(0xFF));

  decoded[decoded_len - 1U] = (uint8_t)(crc >> 8);

  /*
   * Reserve one output byte for the final
   * zero delimiter, which is not part of
   * the COBS encoded body.
   */
  size_t body_length = 0U;

  if (output_capacity < 2U ||
      !cobs_encode(decoded, decoded_len, output, output_capacity - 1U,
                   &body_length)) {
    return PROTOCOL_ERROR_BUFFER;
  }

  /*
   * Terminate the complete encoded packet
   * with exactly one 0x00 delimiter.
   *
   * This lets the receive state machine
   * recognize packet boundaries without
   * relying on UART silence intervals.
   */
  output[body_length] = 0U;

  *output_length = body_length + 1U;

  return PROTOCOL_OK;
}

protocol_result_t protocol_decode(const uint8_t *encoded, size_t encoded_length,
                                  protocol_packet_t *packet) {
  if (encoded == NULL || packet == NULL) {
    return PROTOCOL_ERROR_ARGUMENT;
  }

  /*
   * Decode into private scratch storage.
   *
   * Do not modify the caller's packet until
   * every structural and CRC check has passed.
   */
  uint8_t decoded[PROTOCOL_MAX_DECODED_SIZE];

  size_t decoded_length = 0U;

  if (!cobs_decode(encoded, encoded_length, decoded, sizeof(decoded),
                   &decoded_length)) {
    return PROTOCOL_ERROR_COBS;
  }

  /*
   * A packet must contain at least a six-byte
   * header and two CRC bytes.
   */
  if (decoded_length < PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE) {
    return PROTOCOL_ERROR_LENGTH;
  }

  /*
   * Reject protocol versions whose wire
   * semantics this firmware does not support.
   */
  if (decoded[0] != PROTOCOL_VERSION) {
    return PROTOCOL_ERROR_VERSION;
  }

  /*
   * Only request, response and error packet
   * types are defined in protocol version 1.
   */
  if (decoded[3] > PROTOCOL_TYPE_ERROR) {
    return PROTOCOL_ERROR_TYPE;
  }

  /*
   * The declared payload size must agree
   * exactly with the decoded packet length.
   *
   * This rejects truncated packets, trailing
   * bytes and oversized payload declarations.
   */
  const size_t payload_length = decoded[5];

  if (payload_length > PROTOCOL_MAX_PAYLOAD ||
      decoded_length !=
          PROTOCOL_HEADER_SIZE + payload_length + PROTOCOL_CRC_SIZE) {
    return PROTOCOL_ERROR_LENGTH;
  }

  /*
   * Reconstruct the received little-endian CRC.
   *
   * CRC verification must finish before any
   * decoded packet is given to the dispatcher.
   */
  const uint16_t received_crc =
      (uint16_t)((uint16_t)decoded[decoded_length - 2U] |
                 (uint16_t)((uint16_t)decoded[decoded_length - 1U] << 8));

  const uint16_t calculated_crc =
      crc16_ccitt_false(decoded, decoded_length - PROTOCOL_CRC_SIZE);

  if (calculated_crc != received_crc) {
    return PROTOCOL_ERROR_CRC;
  }

  /*
   * All validations have succeeded.
   *
   * Build a temporary packet and publish
   * it only after every field is valid.
   *
   * Zero-initialization also makes unused
   * payload bytes deterministic.
   */
  protocol_packet_t verified = {0};

  verified.destination = decoded[1];
  verified.sequence = decoded[2];
  verified.type = decoded[3];
  verified.command = decoded[4];
  verified.payload_length = decoded[5];

  memcpy(verified.payload, &decoded[PROTOCOL_HEADER_SIZE], payload_length);

  *packet = verified;

  return PROTOCOL_OK;
}
