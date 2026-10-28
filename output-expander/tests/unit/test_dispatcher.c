#include "dispatcher.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Verify RS-485 unicast address filtering.
 *
 * WHAT:
 * Test matching, mismatched, reserved and uncommissioned
 * addresses, including the highest valid address.
 *
 * WHY:
 * All devices on a shared bus receive the same traffic.
 * Only the device addressed by the master may process
 * a unicast command.
 */
static void test_unicast_routing(void) {
  protocol_packet_t packet = {0};

  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.destination = 0x17U;
  packet.command = DISPATCHER_COMMAND_PING;

  assert(dispatcher_route_request(&packet, 0x17U) ==
         DISPATCHER_UNICAST_REQUEST);

  assert(dispatcher_route_request(&packet, 0x18U) == DISPATCHER_IGNORE);

  /*
   * Uncommissioned devices cannot accept ordinary
   * unicast requests before address assignment.
   */
  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0x00U) == DISPATCHER_IGNORE);

  /*
   * Destination 0xFF is reserved and must never be
   * treated as a valid unicast destination.
   */
  packet.destination = 0xFFU;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  /*
   * 0xFE is the highest legal commissioned address.
   * Test this boundary explicitly.
   */
  packet.destination = 0xFEU;

  assert(dispatcher_route_request(&packet, 0xFEU) ==
         DISPATCHER_UNICAST_REQUEST);
}

/*
 * Verify the special broadcast commissioning route.
 *
 * WHAT:
 * Accept broadcast ASSIGN_UID for both commissioned and
 * uncommissioned devices while rejecting other commands.
 *
 * WHY:
 * ASSIGN_UID must reach devices without known addresses.
 * No broadcast may produce an RS-485 response.
 *
 * Routing eligibility does not mean UID matching or
 * persistent address assignment has succeeded.
 */
static void test_broadcast_routing(void) {
  protocol_packet_t packet = {0};

  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.destination = 0x00U;

  packet.command = DISPATCHER_COMMAND_PING;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  packet.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  packet.command = 0x10U;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  /*
   * ASSIGN_UID is broadcast-capable.
   *
   * The device must still validate the complete UID
   * and new address before changing persistent state.
   */
  packet.command = DISPATCHER_COMMAND_ASSIGN_UID;

  assert(dispatcher_route_request(&packet, 0xFFU) ==
         DISPATCHER_BROADCAST_ASSIGN_UID);

  assert(dispatcher_route_request(&packet, 0x17U) ==
         DISPATCHER_BROADCAST_ASSIGN_UID);
}

/*
 * Verify that only request packets are eligible.
 *
 * WHY:
 * Responses and errors may be visible on the multidrop
 * bus but must never be executed as device commands.
 */
static void test_packet_types(void) {
  protocol_packet_t packet = {0};

  packet.destination = 0x17U;
  packet.command = DISPATCHER_COMMAND_PING;

  packet.type = PROTOCOL_TYPE_RESPONSE;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  packet.type = PROTOCOL_TYPE_ERROR;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  /*
   * Even broadcast ASSIGN_UID must have request type.
   * Matching destination and command is not sufficient.
   */
  packet.destination = 0x00U;
  packet.command = DISPATCHER_COMMAND_ASSIGN_UID;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  packet.type = PROTOCOL_TYPE_RESPONSE;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);
}

/*
 * Verify NULL packet handling before dereferencing data.
 */
static void test_null_packet(void) {
  assert(dispatcher_route_request(NULL, 0x17U) == DISPATCHER_IGNORE);
}

/*
 * Verify a successful PING response.
 *
 * WHAT:
 * Check copied request metadata, all 12 UID bytes,
 * the exact payload length and firmware version.
 *
 * WHY:
 * The master identifies the replying physical MCU
 * by its full UID and associates the reply with the
 * outstanding transaction using its sequence number.
 */
static void test_ping_response(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0x10U, 0x20U, 0x30U, 0x40U,
                                            0x50U, 0x60U, 0x70U, 0x80U,
                                            0x90U, 0xA0U, 0xB0U, 0xC0U};

  const protocol_packet_t request = {.destination = 0x17U,
                                     .sequence = 0x42U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = DISPATCHER_COMMAND_PING,
                                     .payload_length = 0U};

  protocol_packet_t response = {0};

  assert(dispatcher_prepare_ping_response(&request, 0x17U, uid, 2U, 5U,
                                          &response));

  assert(response.destination == request.destination);
  assert(response.sequence == request.sequence);
  assert(response.type == PROTOCOL_TYPE_RESPONSE);
  assert(response.command == DISPATCHER_COMMAND_PING);

  assert(response.payload_length == DISPATCHER_PING_RESPONSE_SIZE);

  /*
   * Compare binary UID bytes, not a C string.
   * The UID can contain arbitrary byte values.
   */
  assert(memcmp(response.payload, uid, DISPATCHER_UID_SIZE) == 0);

  assert(response.payload[12] == 2U);
  assert(response.payload[13] == 5U);
}

/*
 * Verify that invalid PING requests never modify
 * the response packet.
 *
 * WHAT:
 * Fill response storage with a sentinel pattern and
 * compare the complete object after every rejection.
 *
 * WHY:
 * An invalid request must not leave a partially prepared
 * packet that could accidentally be transmitted.
 */
static void test_ping_rejected_requests(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0};

  protocol_packet_t request = {.destination = 0x17U,
                               .sequence = 0x42U,
                               .type = PROTOCOL_TYPE_REQUEST,
                               .command = DISPATCHER_COMMAND_PING,
                               .payload_length = 0U};

  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  /* Wrong destination. */
  request.destination = 0x18U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Broadcast PING is prohibited. */
  request.destination = 0x00U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.destination = 0x17U;

  /* Responses must not execute as requests. */
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Error packets must also be ignored. */
  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.type = PROTOCOL_TYPE_REQUEST;

  /* Incorrect command. */
  request.command = 0x10U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.command = DISPATCHER_COMMAND_PING;

  /* PING requests must contain zero payload bytes. */
  request.payload_length = 1U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.payload_length = 0U;

  /* Uncommissioned devices cannot respond to PING. */
  assert(!dispatcher_prepare_ping_response(&request, 0xFFU, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Verify invalid PING function arguments.
 *
 * WHY:
 * NULL inputs must not cause pointer dereferences.
 * Request and response objects must remain separate
 * to prevent in-place overwriting of incoming fields.
 */
static void test_ping_invalid_arguments(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0};

  protocol_packet_t request = {.destination = 0x17U,
                               .sequence = 0x42U,
                               .type = PROTOCOL_TYPE_REQUEST,
                               .command = DISPATCHER_COMMAND_PING,
                               .payload_length = 0U};

  protocol_packet_t response = {0};

  assert(
      !dispatcher_prepare_ping_response(NULL, 0x17U, uid, 1U, 0U, &response));

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, NULL, 1U, 0U,
                                           &response));

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U, NULL));

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &request));
}

/*
 * Verify a complete PING response encode/decode cycle.
 *
 * WHAT:
 * Build a PING response, serialize it through the
 * production protocol encoder and decode the result.
 *
 * WHY:
 * Checking only the in-memory packet would not verify
 * compatibility with the CRC and COBS wire encoding.
 *
 * The UID includes zero bytes to exercise COBS handling
 * of binary payload data.
 */
static void test_ping_wire_round_trip(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0x00U, 0x01U, 0x02U, 0x00U,
                                            0x04U, 0x05U, 0x06U, 0x07U,
                                            0x00U, 0x09U, 0x0AU, 0x0BU};

  const protocol_packet_t request = {.destination = 0x17U,
                                     .sequence = 0x55U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = DISPATCHER_COMMAND_PING,
                                     .payload_length = 0U};

  protocol_packet_t response = {0};

  assert(dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 2U,
                                          &response));

  /*
   * The encoder includes the protocol version, CRC,
   * COBS encoding and final 0x00 frame delimiter.
   */
  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t wire_length = 0U;

  assert(protocol_encode(&response, wire, sizeof(wire), &wire_length) ==
         PROTOCOL_OK);

  assert(wire_length >= 2U);
  assert(wire_length <= sizeof(wire));
  assert(wire[wire_length - 1U] == 0U);

  /*
   * A COBS-encoded body contains no zero bytes.
   * The only zero is the final frame delimiter.
   */
  for (size_t i = 0U; i < wire_length - 1U; ++i) {
    assert(wire[i] != 0U);
  }

  protocol_packet_t decoded = {0};

  /*
   * The decoder accepts the encoded COBS body
   * without the delimiter, matching frame_receiver.
   */
  assert(protocol_decode(wire, wire_length - 1U, &decoded) == PROTOCOL_OK);

  assert(decoded.destination == request.destination);
  assert(decoded.sequence == request.sequence);
  assert(decoded.type == PROTOCOL_TYPE_RESPONSE);
  assert(decoded.command == DISPATCHER_COMMAND_PING);
  assert(decoded.payload_length == DISPATCHER_PING_RESPONSE_SIZE);

  assert(memcmp(decoded.payload, uid, DISPATCHER_UID_SIZE) == 0);

  assert(decoded.payload[12] == 1U);
  assert(decoded.payload[13] == 2U);
}

/*
 * Verify successful GET_OUTPUTS response construction.
 *
 * WHAT:
 * Test all-off, all-on and mixed logical output masks.
 *
 * WHY:
 * The entire eight-channel state is represented in one
 * byte. Testing bit patterns helps detect accidental
 * inversion or incorrect bit mapping.
 *
 * The tests use logical states, not physical GPIO levels.
 */
static void test_get_outputs_response(void) {
  const protocol_packet_t request = {.destination = 0x17U,
                                     .sequence = 0x63U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = DISPATCHER_COMMAND_GET_OUTPUTS,
                                     .payload_length = 0U};

  protocol_packet_t response = {0};

  /* All channels OFF. */
  assert(dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x00U,
                                                 &response));

  assert(response.destination == request.destination);
  assert(response.sequence == request.sequence);
  assert(response.type == PROTOCOL_TYPE_RESPONSE);
  assert(response.command == DISPATCHER_COMMAND_GET_OUTPUTS);
  assert(response.payload_length == DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE);
  assert(response.payload[0] == 0x00U);

  /* All channels ON. */
  assert(dispatcher_prepare_get_outputs_response(&request, 0x17U, 0xFFU,
                                                 &response));

  assert(response.payload[0] == 0xFFU);

  /*
   * Mixed output state:
   * 0x85 = binary 10000101.
   *
   * Channels 1, 3 and 8 are logically ON.
   */
  assert(dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                 &response));

  assert(response.payload[0] == 0x85U);
}

/*
 * Verify invalid GET_OUTPUTS requests are rejected
 * without changing the caller's response packet.
 *
 * WHY:
 * Wrong-address packets, broadcasts, incorrect commands
 * and invalid payload lengths must not produce replies.
 */
static void test_get_outputs_rejected_requests(void) {
  protocol_packet_t request = {.destination = 0x17U,
                               .sequence = 0x63U,
                               .type = PROTOCOL_TYPE_REQUEST,
                               .command = DISPATCHER_COMMAND_GET_OUTPUTS,
                               .payload_length = 0U};

  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  /* Wrong device address. */
  request.destination = 0x18U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Broadcast GET_OUTPUTS is forbidden. */
  request.destination = 0x00U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.destination = 0x17U;

  /* A response is not an executable request. */
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Error packets must also be ignored. */
  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.type = PROTOCOL_TYPE_REQUEST;

  /* Another command must not be interpreted as GET_OUTPUTS. */
  request.command = DISPATCHER_COMMAND_PING;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  /* GET_OUTPUTS request payload must be empty. */
  request.payload_length = 1U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.payload_length = 0U;

  /* An uncommissioned device cannot answer unicast. */
  assert(!dispatcher_prepare_get_outputs_response(&request, 0xFFU, 0x85U,
                                                  &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Verify defensive argument validation for GET_OUTPUTS.
 *
 * No output packet may be generated when the request
 * or response pointer is invalid.
 */
static void test_get_outputs_invalid_arguments(void) {
  protocol_packet_t request = {.destination = 0x17U,
                               .sequence = 0x63U,
                               .type = PROTOCOL_TYPE_REQUEST,
                               .command = DISPATCHER_COMMAND_GET_OUTPUTS,
                               .payload_length = 0U};

  protocol_packet_t response = {0};

  assert(
      !dispatcher_prepare_get_outputs_response(NULL, 0x17U, 0x85U, &response));

  assert(
      !dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U, NULL));

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x17U, 0x85U,
                                                  &request));
}

/*
 * Verify GET_OUTPUTS compatibility with the wire protocol.
 *
 * WHAT:
 * Construct, encode and decode a one-byte output mask.
 *
 * WHY:
 * The complete wire path must preserve the logical mask,
 * request sequence and response type across CRC and COBS.
 */
static void test_get_outputs_wire_round_trip(void) {
  const protocol_packet_t request = {.destination = 0x17U,
                                     .sequence = 0x77U,
                                     .type = PROTOCOL_TYPE_REQUEST,
                                     .command = DISPATCHER_COMMAND_GET_OUTPUTS,
                                     .payload_length = 0U};

  /*
   * Test a zero mask as well as a mixed mask.
   *
   * A zero payload byte must survive COBS encoding,
   * even though zero is also the wire frame delimiter.
   */
  const uint8_t masks[] = {0x00U, 0x85U, 0xFFU};

  for (size_t i = 0U; i < sizeof(masks); ++i) {
    protocol_packet_t response = {0};

    assert(dispatcher_prepare_get_outputs_response(&request, 0x17U, masks[i],
                                                   &response));

    uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
    size_t wire_length = 0U;

    assert(protocol_encode(&response, wire, sizeof(wire), &wire_length) ==
           PROTOCOL_OK);

    assert(wire_length >= 2U);
    assert(wire_length <= sizeof(wire));
    assert(wire[wire_length - 1U] == 0U);

    /*
     * COBS must remove all embedded zero bytes from
     * the encoded body, regardless of output mask.
     */
    for (size_t j = 0U; j < wire_length - 1U; ++j) {
      assert(wire[j] != 0U);
    }

    protocol_packet_t decoded = {0};

    assert(protocol_decode(wire, wire_length - 1U, &decoded) == PROTOCOL_OK);

    assert(decoded.destination == request.destination);
    assert(decoded.sequence == request.sequence);
    assert(decoded.type == PROTOCOL_TYPE_RESPONSE);
    assert(decoded.command == DISPATCHER_COMMAND_GET_OUTPUTS);
    assert(decoded.payload_length == DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE);

    assert(decoded.payload[0] == masks[i]);
  }
}

/*
 * Run all dispatcher tests.
 *
 * WHAT:
 * Execute every routing, PING and GET_OUTPUTS test.
 *
 * WHY:
 * Static test functions do not run automatically.
 * Calling them here allows assert() failures to terminate
 * the executable and be reported through CTest.
 *
 * The host-debug configuration also enables AddressSanitizer
 * and UndefinedBehaviorSanitizer.
 */
int main(void) {
  /* Request routing and address filtering. */
  test_unicast_routing();
  test_broadcast_routing();
  test_packet_types();
  test_null_packet();

  /* PING response construction and wire compatibility. */
  test_ping_response();
  test_ping_rejected_requests();
  test_ping_invalid_arguments();
  test_ping_wire_round_trip();

  /* GET_OUTPUTS response construction and wire compatibility. */
  test_get_outputs_response();
  test_get_outputs_rejected_requests();
  test_get_outputs_invalid_arguments();
  test_get_outputs_wire_round_trip();

  return 0;
}
