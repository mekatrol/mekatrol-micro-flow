
#ifndef OUTPUT_EXPANDER_PROTOCOL_H
#define OUTPUT_EXPANDER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Protocol version 1 wire-format limits.
 *
 * Keep these values identical to protocol.rs
 * and the Python host implementation.
 *
 * Fixed capacities avoid heap allocation and
 * limit memory usage on the CH32V003.
 */
#define PROTOCOL_VERSION 1U
#define PROTOCOL_HEADER_SIZE 6U
#define PROTOCOL_CRC_SIZE 2U
#define PROTOCOL_MAX_PAYLOAD 24U
#define PROTOCOL_MAX_DECODED_SIZE 32U
#define PROTOCOL_MAX_ENCODED_SIZE 34U

/*
 * Packet types defined by protocol version 1.
 *
 * The device processes requests; responses and
 * errors are generated for the master.
 */
#define PROTOCOL_TYPE_REQUEST 0U
#define PROTOCOL_TYPE_RESPONSE 1U
#define PROTOCOL_TYPE_ERROR 2U

/*
 * A decoded packet with its own payload storage.
 *
 * WHAT:
 * Represents one packet independently of UART
 * and COBS buffers.
 *
 * WHY:
 * The foreground can reuse receive buffers
 * without invalidating packet payload data.
 *
 * Only payload_length bytes are meaningful.
 */
typedef struct {
  uint8_t destination;
  uint8_t sequence;
  uint8_t type;
  uint8_t command;
  uint8_t payload_length;
  uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} protocol_packet_t;

/*
 * Packet validation results.
 *
 * Separate errors allow host tests to distinguish
 * malformed COBS, incorrect lengths, unsupported
 * versions, invalid types and CRC failures.
 */
typedef enum {
  PROTOCOL_OK = 0,
  PROTOCOL_ERROR_ARGUMENT,
  PROTOCOL_ERROR_COBS,
  PROTOCOL_ERROR_LENGTH,
  PROTOCOL_ERROR_VERSION,
  PROTOCOL_ERROR_TYPE,
  PROTOCOL_ERROR_CRC,
  PROTOCOL_ERROR_BUFFER
} protocol_result_t;

/*
 * Encode a packet into a complete wire frame.
 *
 * The output includes the trailing 0x00 delimiter.
 * No USART transmission occurs in this function.
 *
 * On failure, *output_length is zero and the
 * output buffer must be treated as invalid.
 *
 * Caller must provide non-overlapping buffers.
 */
protocol_result_t protocol_encode(const protocol_packet_t *packet,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length);

/*
 * Decode a COBS body after its delimiter has
 * already been removed by the frame receiver.
 *
 * Validates the version, type, declared length
 * and CRC before publishing a decoded packet.
 *
 * The destination packet is unchanged on error.
 */
protocol_result_t protocol_decode(const uint8_t *encoded, size_t encoded_length,
                                  protocol_packet_t *packet);

#endif
