
#include "response_tx.h"

#include "dispatcher.h"
#include "protocol.h"
#include "usart.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Simulated USART transmission state.
 *
 * WHAT:
 * Record the bytes that the production response
 * transmitter attempts to send.
 *
 * WHY:
 * Host tests cannot access CH32V003 USART registers
 * or control the real RS-485 transceiver.
 *
 * Recording whole frames verifies the boundary
 * between portable protocol encoding and hardware
 * transmission without introducing a second encoder.
 */
static uint8_t transmitted[PROTOCOL_MAX_ENCODED_SIZE];
static size_t transmitted_length;
static unsigned int transmission_count;

/*
 * Reset the simulated hardware before each test.
 *
 * Filling the buffer with a recognizable pattern
 * helps detect accidental partial writes.
 */
static void reset_transmitter(void) {
  memset(transmitted, 0xA5, sizeof(transmitted));
  transmitted_length = 0U;
  transmission_count = 0U;
}

/*
 * Replace the real USART driver's blocking send.
 *
 * WHAT:
 * Capture a complete output frame and count calls.
 *
 * WHY:
 * This checks that response_tx_send() invokes the
 * USART exactly once for a valid response and never
 * invokes it for a rejected response.
 *
 * A failing assertion also protects the test buffer
 * from overflowing if a regression changes the
 * maximum frame length.
 *
 * This mock does not simulate DE timing or USART TC.
 * Those remain hardware-driver verification tasks.
 */
void usart_write_response(const uint8_t *data, size_t length) {
  assert(data != NULL);
  assert(length > 0U);
  assert(length <= sizeof(transmitted));

  ++transmission_count;
  transmitted_length = length;

  memcpy(transmitted, data, length);
}

/*
 * Construct a deterministic valid response packet.
 *
 * The sequence and command are deliberately nonzero
 * so their preservation can be verified after framing.
 */
static protocol_packet_t make_response(void) {
  protocol_packet_t packet = {0};

  packet.destination = 0x23U;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_RESPONSE;
  packet.command = 0x11U;
  packet.payload_length = 1U;
  packet.payload[0] = 0x85U;

  return packet;
}

/*
 * Verify that the captured bytes constitute exactly
 * one valid encoded packet with a trailing delimiter.
 *
 * WHAT:
 * Check frame capacity, delimiter, COBS properties,
 * CRC, and every decoded packet field.
 *
 * WHY:
 * A valid response must be recognizable by the master.
 * Checking only that USART was called would miss
 * incorrect serialization or missing termination.
 *
 * protocol_decode() expects the COBS body without
 * the delimiter, so pass transmitted_length - 1.
 */
static void assert_transmitted_packet(const protocol_packet_t *expected) {

  assert(transmission_count == 1U);
  assert(transmitted_length >= 2U);
  assert(transmitted_length <= PROTOCOL_MAX_ENCODED_SIZE);

  for (size_t i = 0U; i + 1U < transmitted_length; ++i) {
    assert(transmitted[i] != 0U);
  }

  assert(transmitted[transmitted_length - 1U] == 0U);

  protocol_packet_t decoded = {0};

  assert(protocol_decode(transmitted, transmitted_length - 1U, &decoded) ==
         PROTOCOL_OK);

  assert(decoded.destination == expected->destination);
  assert(decoded.sequence == expected->sequence);
  assert(decoded.type == expected->type);
  assert(decoded.command == expected->command);
  assert(decoded.payload_length == expected->payload_length);

  assert(memcmp(decoded.payload, expected->payload, expected->payload_length) ==
         0);
}

/*
 * Verify the normal GET_OUTPUTS response path.
 *
 * The response must preserve the requested logical
 * output mask without inverting active-low polarity.
 *
 * Logical-to-physical conversion belongs exclusively
 * to the GPIO driver, not to packet serialization.
 */
static void test_success_response(void) {
  reset_transmitter();

  const protocol_packet_t packet = make_response();

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);

  assert(transmitted_length ==
         PROTOCOL_HEADER_SIZE + PROTOCOL_CRC_SIZE + packet.payload_length + 2U);
}

/*
 * Verify a protocol error response.
 *
 * A one-byte error-code payload is required by v1.
 * Error responses must use TYPE=ERROR, not TYPE=RESPONSE.
 */
static void test_error_response(void) {
  reset_transmitter();

  protocol_packet_t packet = make_response();

  packet.type = PROTOCOL_TYPE_ERROR;
  packet.command = 0xEEU;
  packet.payload[0] = 0x01U;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);
}

/*
 * Verify that COBS correctly handles multiple zero bytes.
 *
 * WHY:
 * Error codes, command identifiers, output states and
 * sequence numbers can all contain zero. None of those
 * bytes may be confused with the frame delimiter.
 */
static void test_zero_bytes(void) {
  reset_transmitter();

  protocol_packet_t packet = make_response();

  packet.sequence = 0U;
  packet.command = 0x10U;
  packet.payload_length = 4U;

  packet.payload[0] = 0U;
  packet.payload[1] = 0x80U;
  packet.payload[2] = 0U;
  packet.payload[3] = 0xFFU;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);
}

/*
 * Exercise all sequence numbers.
 *
 * WHAT:
 * Verify that sequence 0, sequence 255, and every
 * value between are transmitted unchanged.
 *
 * WHY:
 * Transaction identifiers wrap modulo 256.
 * Neither this module nor the COBS encoder may
 * mistake sequence zero for a missing transaction.
 */
static void test_all_sequences(void) {
  for (unsigned int value = 0U; value < 256U; ++value) {
    reset_transmitter();

    protocol_packet_t packet = make_response();
    packet.sequence = (uint8_t)value;

    assert(response_tx_send(&packet));
    assert_transmitted_packet(&packet);
  }
}

/*
 * Verify maximum payload and frame capacities.
 *
 * The v1 maximum is:
 *   6 header + 24 payload + 2 CRC = 32 decoded bytes.
 *
 * The COBS body and delimiter must fit within
 * PROTOCOL_MAX_ENCODED_SIZE.
 *
 * Exercise a maximum-size nonzero payload and a
 * maximum-size payload containing embedded zeros.
 */
static void test_maximum_payload(void) {
  protocol_packet_t packet = make_response();

  packet.payload_length = PROTOCOL_MAX_PAYLOAD;

  for (size_t i = 0U; i < PROTOCOL_MAX_PAYLOAD; ++i) {
    packet.payload[i] = (uint8_t)(i + 1U);
  }

  reset_transmitter();

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);

  for (size_t i = 0U; i < PROTOCOL_MAX_PAYLOAD; ++i) {
    packet.payload[i] = 0U;
  }

  reset_transmitter();

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);
}

/*
 * Confirm no bus activity occurs for missing packet
 * storage or reserved destination addresses.
 *
 * The shared RS-485 bus must never see responses
 * addressed to broadcast 0x00 or ordinary commands at 0xFF.
 */
static void test_invalid_destination(void) {
  reset_transmitter();

  assert(!response_tx_send(NULL));
  assert(transmission_count == 0U);

  protocol_packet_t packet = make_response();

  packet.destination = 0x00U;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.destination = 0xFFU;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.type = PROTOCOL_TYPE_RESPONSE;
  packet.command = DISPATCHER_COMMAND_PING;
  packet.payload_length = DISPATCHER_PING_RESPONSE_SIZE;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);

  reset_transmitter();

  packet.type = PROTOCOL_TYPE_ERROR;
  packet.payload_length = 1U;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  /*
   * Both ends of the commissioned address range
   * are permitted as normal unicast addresses.
   */
  packet.destination = 0x01U;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);

  reset_transmitter();

  packet.destination = 0xFEU;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);
}

/*
 * Reject request packets and undefined packet types.
 *
 * WHY:
 * Devices cannot initiate requests on the single-master
 * bus through the response transmission interface.
 */
static void test_invalid_packet_type(void) {
  reset_transmitter();

  protocol_packet_t packet = make_response();

  packet.type = PROTOCOL_TYPE_REQUEST;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.type = 0x03U;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.type = 0xFFU;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);
}

/*
 * Check invalid payload declarations.
 *
 * An oversized packet must not be read past the
 * fixed 24-byte payload array or transmitted.
 *
 * Error responses must contain exactly one byte.
 */
static void test_invalid_payload_lengths(void) {
  reset_transmitter();

  protocol_packet_t packet = make_response();

  packet.payload_length = (uint8_t)(PROTOCOL_MAX_PAYLOAD + 1U);

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.type = PROTOCOL_TYPE_ERROR;
  packet.payload_length = 0U;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.payload_length = 2U;

  assert(!response_tx_send(&packet));
  assert(transmission_count == 0U);

  packet.payload_length = 1U;

  assert(response_tx_send(&packet));
  assert_transmitted_packet(&packet);
}

/*
 * Run all foreground response transmission tests.
 *
 * Host AddressSanitizer and UndefinedBehaviorSanitizer
 * remain enabled through the existing CMake helpers.
 *
 * This test validates the software transmission boundary.
 * Real USART TC behavior requires separate hardware tests.
 */
int main(void) {
  test_success_response();
  test_error_response();
  test_zero_bytes();
  test_all_sequences();
  test_maximum_payload();
  test_invalid_destination();
  test_invalid_packet_type();
  test_invalid_payload_lengths();

  return 0;
}
