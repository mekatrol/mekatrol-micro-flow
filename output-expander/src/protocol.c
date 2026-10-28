
#include "protocol.h"

#include "cobs.h"
#include "crc16.h"

#include <stddef.h>
#include <stdint.h>

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
   * Every byte in the transmitted prefix is explicitly written
   * below, so zero-initializing the entire buffer is unnecessary.
   *
   * WHY:
   * Avoid an unnecessary memory clear on our bare-metal target.
   */
  uint8_t decoded[PROTOCOL_MAX_DECODED_SIZE];

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
   * Copy only the meaningful payload bytes.
   *
   * WHY:
   * A bounded loop avoids requiring libc's memcpy().
   * The maximum payload is 24 bytes.
   */
  for (size_t i = 0U; i < payload_len; ++i) {
    decoded[PROTOCOL_HEADER_SIZE + i] = packet->payload[i];
  }

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
   * Build a temporary packet and publish it only after
   * every field has been populated.
   *
   * WHY:
   * Explicitly initializing the payload avoids both
   * memset() and memcpy() dependencies. Unused payload
   * bytes remain deterministically zero.
   */
  protocol_packet_t verified;

  verified.destination = decoded[1];
  verified.sequence = decoded[2];
  verified.type = decoded[3];
  verified.command = decoded[4];
  verified.payload_length = decoded[5];

  /*
   * Copy the received payload and zero unused positions.
   *
   * Each of the 24 bytes is initialized before the structure
   * is assigned to the caller's output packet.
   */
  for (size_t i = 0U; i < PROTOCOL_MAX_PAYLOAD; ++i) {
    if (i < payload_length) {
      verified.payload[i] = decoded[PROTOCOL_HEADER_SIZE + i];
    } else {
      verified.payload[i] = 0U;
    }
  }

  /*
   * Publish only after successful validation.
   * A malformed packet leaves the caller's data unchanged.
   */
  *packet = verified;

  return PROTOCOL_OK;
}
