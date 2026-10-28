
#include "dispatcher.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Construct a deterministic unicast request for testing.
 *
 * WHAT:
 * Initialize a protocol packet with a chosen command and
 * the standard test destination and sequence number.
 *
 * WHY:
 * Each test starts with known packet contents, avoiding
 * accidental dependencies on previous test cases.
 */
static protocol_packet_t make_request(uint8_t command) {
  protocol_packet_t packet = {0};

  packet.destination = 0x23U;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.command = command;

  return packet;
}

/*
 * Verify that a rejected command leaves the entire
 * response object unchanged.
 *
 * The sentinel bytes make accidental partial writes
 * detectable, including writes to unused payload space.
 */
static void fill_response_sentinel(protocol_packet_t *response) {
  memset(response, 0xA5, sizeof(*response));
}

static void assert_response_unchanged(const protocol_packet_t *response,
                                      const protocol_packet_t *original) {
  assert(memcmp(response, original, sizeof(*response)) == 0);
}

/*
 * Verify all common success response header fields.
 *
 * The master identifies the outstanding transaction
 * using destination, sequence number and command.
 */
static void assert_response_header(const protocol_packet_t *request,
                                   const protocol_packet_t *response,
                                   uint8_t expected_length) {
  assert(response->destination == request->destination);
  assert(response->sequence == request->sequence);
  assert(response->type == PROTOCOL_TYPE_RESPONSE);
  assert(response->command == request->command);
  assert(response->payload_length == expected_length);
}

/*
 * Exercise the production CRC and COBS encoder/decoder.
 *
 * WHAT:
 * Serialize a response to its complete wire frame,
 * verify the delimiter and decode it again.
 *
 * WHY:
 * A correct in-memory response must also survive the
 * existing Rust-compatible wire encoding.
 *
 * The decoder receives the encoded body without the
 * trailing zero delimiter, as in frame_receiver_poll().
 */
static protocol_packet_t round_trip(const protocol_packet_t *response) {
  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t wire_length = 0U;

  assert(protocol_encode(response, wire, sizeof(wire), &wire_length) ==
         PROTOCOL_OK);

  assert(wire_length >= 2U);
  assert(wire_length <= sizeof(wire));
  assert(wire[wire_length - 1U] == 0U);

  /*
   * COBS removes zero bytes from the encoded body.
   * Only the final byte is the frame delimiter.
   */
  for (size_t i = 0U; i < wire_length - 1U; ++i) {
    assert(wire[i] != 0U);
  }

  protocol_packet_t decoded = {0};

  assert(protocol_decode(wire, wire_length - 1U, &decoded) == PROTOCOL_OK);

  return decoded;
}

/*
 * Verify unicast address filtering and reserved addresses.
 *
 * A device must not act on packets addressed to another
 * expander or accept unicast traffic before commissioning.
 */
static void test_unicast_routing(void) {
  protocol_packet_t packet = make_request(DISPATCHER_COMMAND_PING);

  assert(dispatcher_route_request(&packet, 0x23U) ==
         DISPATCHER_UNICAST_REQUEST);

  assert(dispatcher_route_request(&packet, 0x18U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0x00U) == DISPATCHER_IGNORE);

  packet.destination = 0xFFU;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) ==
         DISPATCHER_UNICAST_REQUEST);

  packet.command = DISPATCHER_COMMAND_GET_OUTPUTS;
  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  /*
   * 0xFE is the highest valid commissioned address.
   * Include this boundary to catch off-by-one errors.
   */
  packet.destination = 0xFEU;

  assert(dispatcher_route_request(&packet, 0xFEU) ==
         DISPATCHER_UNICAST_REQUEST);
}

/*
 * Only ASSIGN_ADDRESS_BY_UID may be routed from broadcast traffic.
 *
 * A broadcast commissioning request is not a permission
 * to alter the address until the full UID is checked.
 *
 * No broadcast packet may generate a response.
 */
static void test_broadcast_routing(void) {
  protocol_packet_t packet = make_request(DISPATCHER_COMMAND_PING);

  packet.destination = 0x00U;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  packet.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  packet.command = DISPATCHER_COMMAND_SET_OUTPUTS;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  packet.command = DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID;

  assert(dispatcher_route_request(&packet, 0x23U) ==
         DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID);

  assert(dispatcher_route_request(&packet, 0xFFU) ==
         DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID);
}

/*
 * Responses and error packets are never device commands.
 *
 * Multiple devices can observe master and slave traffic
 * on the same bus, so packet type must be checked before
 * routing any command.
 */
static void test_packet_types(void) {
  protocol_packet_t packet = make_request(DISPATCHER_COMMAND_PING);

  packet.type = PROTOCOL_TYPE_RESPONSE;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  packet.type = PROTOCOL_TYPE_ERROR;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);

  packet.destination = 0x00U;
  packet.command = DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID;

  assert(dispatcher_route_request(&packet, 0x23U) == DISPATCHER_IGNORE);
}

static void test_null_packet(void) {
  assert(dispatcher_route_request(NULL, 0x23U) == DISPATCHER_IGNORE);
}

/*
 * Verify PING response layout, including all UID bytes,
 * firmware version, copied header fields and CRC/COBS.
 *
 * Embedded zeros in the UID exercise COBS binary handling.
 */
static void test_ping_response(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0x00U, 0x11U, 0x22U, 0x00U,
                                            0x44U, 0x55U, 0x66U, 0x77U,
                                            0x00U, 0x99U, 0xAAU, 0xBBU};

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_PING);

  protocol_packet_t response = {0};

  assert(dispatcher_prepare_ping_response(&request, 0x23U, uid, 2U, 5U,
                                          &response));

  assert_response_header(&request, &response, DISPATCHER_PING_RESPONSE_SIZE);

  assert(memcmp(response.payload, uid, DISPATCHER_UID_SIZE) == 0);

  assert(response.payload[12] == 2U);
  assert(response.payload[13] == 5U);

  protocol_packet_t decoded = round_trip(&response);

  assert_response_header(&request, &decoded, DISPATCHER_PING_RESPONSE_SIZE);

  assert(memcmp(decoded.payload, uid, DISPATCHER_UID_SIZE) == 0);

  assert(decoded.payload[12] == 2U);
  assert(decoded.payload[13] == 5U);
}

/*
 * Verify invalid PING requests never construct a response.
 *
 * A request can pass CRC validation but still have
 * an invalid command, destination or payload length.
 */
static void test_ping_rejected_requests(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0};

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_PING);

  protocol_packet_t response;
  fill_response_sentinel(&response);

  const protocol_packet_t original = response;

  request.destination = 0x18U;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x00U;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x23U;
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_REQUEST;
  request.command = DISPATCHER_COMMAND_SET_OUTPUTS;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.command = DISPATCHER_COMMAND_PING;
  request.payload_length = 1U;

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);

  request.payload_length = 0U;

  assert(!dispatcher_prepare_ping_response(&request, 0xFFU, uid, 1U, 0U,
                                           &response));

  assert_response_unchanged(&response, &original);
}

/*
 * Check NULL arguments and unsupported in-place writes.
 *
 * Defensive validation keeps invalid caller arguments
 * from causing undefined behavior.
 */
static void test_ping_invalid_arguments(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0};

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_PING);

  protocol_packet_t response = {0};

  assert(
      !dispatcher_prepare_ping_response(NULL, 0x23U, uid, 1U, 0U, &response));

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, NULL, 1U, 0U,
                                           &response));

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U, NULL));

  assert(!dispatcher_prepare_ping_response(&request, 0x23U, uid, 1U, 0U,
                                           &request));
}

/*
 * GET_OUTPUTS must return the existing logical state.
 *
 * Exercise all-off, all-on and mixed masks, including
 * zero bytes that must survive COBS encoding.
 */
static void test_get_outputs_response(void) {
  const uint8_t masks[] = {0x00U, 0x85U, 0xFFU};

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_GET_OUTPUTS);

  for (size_t i = 0U; i < sizeof(masks); ++i) {
    protocol_packet_t response = {0};

    assert(dispatcher_prepare_get_outputs_response(&request, 0x23U, masks[i],
                                                   &response));

    assert_response_header(&request, &response,
                           DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE);

    assert(response.payload[0] == masks[i]);

    protocol_packet_t decoded = round_trip(&response);

    assert_response_header(&request, &decoded,
                           DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE);

    assert(decoded.payload[0] == masks[i]);
  }
}

/*
 * Reject malformed and unrelated GET_OUTPUTS requests.
 *
 * Output packets must remain unchanged on failure.
 */
static void test_get_outputs_rejected_requests(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_GET_OUTPUTS);

  protocol_packet_t response;
  fill_response_sentinel(&response);

  const protocol_packet_t original = response;

  request.destination = 0x18U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x00U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x23U;
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_REQUEST;
  request.command = DISPATCHER_COMMAND_PING;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.command = DISPATCHER_COMMAND_GET_OUTPUTS;
  request.payload_length = 1U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.payload_length = 0U;

  assert(!dispatcher_prepare_get_outputs_response(&request, 0xFFU, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);
}

static void test_get_outputs_invalid_arguments(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_GET_OUTPUTS);

  protocol_packet_t response = {0};

  assert(
      !dispatcher_prepare_get_outputs_response(NULL, 0x23U, 0x85U, &response));

  assert(
      !dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U, NULL));

  assert(!dispatcher_prepare_get_outputs_response(&request, 0x23U, 0x85U,
                                                  &request));
}

/*
 * Verify the complete SET_OUTPUTS validation interface.
 *
 * WHAT:
 * Supply legal one-byte output masks, including the
 * minimum, maximum and every single output bit.
 *
 * WHY:
 * All eight channels must be independently addressable.
 * Testing single-bit masks catches incorrect bit indexing.
 *
 * A mask of 0xFF is valid because all eight outputs
 * may be logically ON simultaneously.
 */
static void test_set_outputs_parse_valid(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;

  const uint8_t masks[] = {0x00U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U,
                           0x20U, 0x40U, 0x80U, 0x85U, 0xFFU};

  for (size_t i = 0U; i < sizeof(masks); ++i) {
    request.payload[0] = masks[i];

    uint8_t requested_mask = 0x5AU;

    assert(dispatcher_parse_set_outputs(&request, 0x23U, &requested_mask));

    /*
     * The dispatcher must preserve the exact logical
     * mask. No active-low inversion is performed here.
     */
    assert(requested_mask == masks[i]);
  }
}

/*
 * Verify that invalid SET_OUTPUTS requests do not
 * publish an output mask.
 *
 * WHY:
 * The foreground must never use malformed packet data
 * to change the state of physical output channels.
 */
static void test_set_outputs_parse_invalid(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0x85U;

  uint8_t mask = 0x5AU;

  /* A wrong-address request must not change state. */
  request.destination = 0x18U;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  /* Broadcast SET_OUTPUTS is forbidden. */
  request.destination = 0x00U;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  /* Destination 0xFF is reserved. */
  request.destination = 0xFFU;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  request.destination = 0x23U;

  /* No ordinary unicast commands before commissioning. */
  assert(!dispatcher_parse_set_outputs(&request, 0xFFU, &mask));
  assert(mask == 0x5AU);

  /* A response is not an executable request. */
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  request.type = PROTOCOL_TYPE_REQUEST;

  /* The command identifier must match SET_OUTPUTS. */
  request.command = DISPATCHER_COMMAND_PING;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  request.command = DISPATCHER_COMMAND_SET_OUTPUTS;

  /*
   * Zero and two-byte payloads are invalid.
   * SET_OUTPUTS requires exactly one byte.
   */
  request.payload_length = 0U;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);

  request.payload_length = 2U;

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, &mask));
  assert(mask == 0x5AU);
}

/*
 * Verify NULL argument handling for SET_OUTPUTS.
 *
 * An invalid request pointer or destination pointer
 * must be rejected without dereferencing it.
 */
static void test_set_outputs_parse_arguments(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0x85U;

  uint8_t mask = 0x5AU;

  assert(!dispatcher_parse_set_outputs(NULL, 0x23U, &mask));

  assert(mask == 0x5AU);

  assert(!dispatcher_parse_set_outputs(&request, 0x23U, NULL));
}

/*
 * Verify SET_OUTPUTS response construction.
 *
 * WHAT:
 * Construct acknowledgments for all-off, all-on
 * and mixed output masks.
 *
 * WHY:
 * The master expects the mask that was actually
 * applied, copied into the response payload.
 *
 * Complete CRC and COBS round trips also ensure
 * the encoded response remains protocol-compatible.
 */
static void test_set_outputs_response(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;

  const uint8_t masks[] = {0x00U, 0x85U, 0xFFU};

  for (size_t i = 0U; i < sizeof(masks); ++i) {
    request.payload[0] = masks[i];

    protocol_packet_t response = {0};

    assert(dispatcher_prepare_set_outputs_response(&request, 0x23U, masks[i],
                                                   &response));

    assert_response_header(&request, &response,
                           DISPATCHER_SET_OUTPUTS_RESPONSE_SIZE);

    assert(response.payload[0] == masks[i]);

    protocol_packet_t decoded = round_trip(&response);

    assert_response_header(&request, &decoded,
                           DISPATCHER_SET_OUTPUTS_RESPONSE_SIZE);

    assert(decoded.payload[0] == masks[i]);
  }
}

/*
 * Verify that an applied-mask mismatch cannot produce
 * a success acknowledgment.
 *
 * WHY:
 * A success response must never claim that requested
 * outputs changed if the requested state was not achieved.
 *
 * This test does not simulate actual GPIO faults; it
 * verifies the portable protocol-level consistency check.
 */
static void test_set_outputs_applied_mismatch(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0x85U;

  protocol_packet_t response;
  fill_response_sentinel(&response);

  const protocol_packet_t original = response;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x00U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0xFFU,
                                                  &response));

  assert_response_unchanged(&response, &original);

  /*
   * An exact applied mask is accepted.
   * The response is now a complete success packet.
   */
  assert(dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                 &response));

  assert(response.payload[0] == 0x85U);
}

/*
 * Verify that a malformed SET_OUTPUTS request cannot
 * generate a success acknowledgment.
 *
 * The response object must be untouched on rejection.
 */
static void test_set_outputs_response_invalid(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0x85U;

  protocol_packet_t response;
  fill_response_sentinel(&response);

  const protocol_packet_t original = response;

  request.destination = 0x18U;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x00U;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.destination = 0x23U;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0xFFU, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_ERROR;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.type = PROTOCOL_TYPE_REQUEST;
  request.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.command = DISPATCHER_COMMAND_SET_OUTPUTS;
  request.payload_length = 0U;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);

  request.payload_length = 2U;

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &response));

  assert_response_unchanged(&response, &original);
}

/*
 * Validate SET_OUTPUTS response pointer arguments.
 *
 * Input and output packet objects cannot overlap
 * because the response builder overwrites packet fields.
 */
static void test_set_outputs_response_arguments(void) {
  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0x85U;

  protocol_packet_t response = {0};

  assert(
      !dispatcher_prepare_set_outputs_response(NULL, 0x23U, 0x85U, &response));

  assert(
      !dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U, NULL));

  assert(!dispatcher_prepare_set_outputs_response(&request, 0x23U, 0x85U,
                                                  &request));
}

/*
 * Execute every dispatcher unit test.
 *
 * WHAT:
 * Test routing, read-only response construction,
 * SET_OUTPUTS validation and success responses.
 *
 * WHY:
 * Static functions do not run automatically.
 * Explicit calls ensure the compiler does not
 * discard them and that CTest observes failures.
 *
 * Host-debug also enables AddressSanitizer and
 * UndefinedBehaviorSanitizer for runtime checking.
 */
int main(void) {
  /* Shared RS-485 routing rules. */
  test_unicast_routing();
  test_broadcast_routing();
  test_packet_types();
  test_null_packet();

  /* PING command. */
  test_ping_response();
  test_ping_rejected_requests();
  test_ping_invalid_arguments();

  /* GET_OUTPUTS command. */
  test_get_outputs_response();
  test_get_outputs_rejected_requests();
  test_get_outputs_invalid_arguments();

  /* SET_OUTPUTS request validation. */
  test_set_outputs_parse_valid();
  test_set_outputs_parse_invalid();
  test_set_outputs_parse_arguments();

  /* SET_OUTPUTS acknowledgment construction. */
  test_set_outputs_response();
  test_set_outputs_applied_mismatch();
  test_set_outputs_response_invalid();
  test_set_outputs_response_arguments();

  return 0;
}
