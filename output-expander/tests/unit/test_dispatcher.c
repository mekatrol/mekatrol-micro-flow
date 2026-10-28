#include "dispatcher.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Verify that a commissioned device accepts only
 * unicast requests addressed to itself.
 *
 * WHAT:
 * Check matching addresses, other device addresses,
 * and both reserved address values.
 *
 * WHY:
 * Every receiver on a multidrop RS-485 bus sees the
 * same traffic. Incorrect address filtering could
 * allow multiple expanders to execute one command
 * or transmit simultaneously, causing bus contention.
 */
static void test_unicast_routing(void) {
  protocol_packet_t packet = {0};

  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.destination = 0x17U;
  packet.command = 0x01U;

  /* A correctly addressed request must be accepted. */
  assert(dispatcher_route_request(&packet, 0x17U) ==
         DISPATCHER_UNICAST_REQUEST);

  /* A request for another device must be ignored. */
  assert(dispatcher_route_request(&packet, 0x18U) == DISPATCHER_IGNORE);

  /*
   * An uncommissioned device must not process
   * ordinary unicast commands before receiving
   * its assigned address.
   */
  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  /*
   * The broadcast address is not a valid
   * commissioned device address.
   */
  assert(dispatcher_route_request(&packet, 0x00U) == DISPATCHER_IGNORE);

  /*
   * Destination 0xFF is reserved for the internal
   * uncommissioned state. It is never a valid
   * unicast destination.
   */
  packet.destination = 0xFFU;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  /*
   * Check the highest valid commissioned address
   * to catch an accidental exclusion of 0xFE.
   */
  packet.destination = 0xFEU;

  assert(dispatcher_route_request(&packet, 0xFEU) ==
         DISPATCHER_UNICAST_REQUEST);
}

/*
 * Verify the protocol's special broadcast rules.
 *
 * WHAT:
 * Permit ASSIGN_UID on destination 0x00, while
 * rejecting every other tested broadcast command.
 *
 * WHY:
 * An uncommissioned device must be reachable for
 * UID-based commissioning without first knowing
 * its individual bus address.
 *
 * Broadcast requests must never produce replies;
 * the dispatcher therefore returns a distinct route
 * rather than treating broadcast as ordinary unicast.
 *
 * UID validation and address persistence are NOT
 * performed by this routing function.
 */
static void test_broadcast_routing(void) {
  protocol_packet_t packet = {0};

  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.destination = 0x00U;

  /* PING is not permitted as a broadcast command. */
  packet.command = 0x01U;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  assert(dispatcher_route_request(&packet, 0xFFU) == DISPATCHER_IGNORE);

  /* SET_OUTPUTS must never operate through broadcast. */
  packet.command = 0x10U;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  /* ASSIGN_UID is explicitly broadcast-capable. */
  packet.command = 0x30U;

  assert(dispatcher_route_request(&packet, 0xFFU) ==
         DISPATCHER_BROADCAST_ASSIGN_UID);

  assert(dispatcher_route_request(&packet, 0x17U) ==
         DISPATCHER_BROADCAST_ASSIGN_UID);
}

/*
 * Verify that device firmware ignores packets
 * that are not requests from the bus master.
 *
 * WHY:
 * Responses and error packets may be visible to
 * every device on the shared RS-485 bus.
 *
 * They must never be interpreted as commands,
 * even if their destination address matches.
 */
static void test_packet_types(void) {
  protocol_packet_t packet = {0};

  packet.destination = 0x17U;
  packet.command = 0x01U;

  packet.type = PROTOCOL_TYPE_RESPONSE;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  packet.type = PROTOCOL_TYPE_ERROR;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);

  /*
   * Repeat the check for broadcast commissioning.
   * Packet type filtering must take priority over
   * command and destination matching.
   */
  packet.destination = 0x00U;
  packet.command = 0x30U;

  assert(dispatcher_route_request(&packet, 0x17U) == DISPATCHER_IGNORE);
}

/*
 * A null packet pointer must be rejected before
 * any packet fields are accessed.
 *
 * WHY:
 * This gives the portable dispatcher a defined
 * failure result for an invalid caller argument,
 * rather than dereferencing an invalid pointer.
 */
static void test_null_packet(void) {
  assert(dispatcher_route_request(NULL, 0x17U) == DISPATCHER_IGNORE);
}

/*
 * Verify construction of a successful PING response.
 *
 * WHAT:
 * Supply a valid unicast PING request, a known 12-byte
 * UID and explicit firmware version numbers.
 *
 * WHY:
 * A deterministic UID allows every response byte to
 * be checked on the host, independently of CH32V003
 * UID registers or actual RS-485 hardware.
 *
 * The response must preserve the request's destination,
 * sequence and command so the master can identify the
 * transaction to which the response belongs.
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

  /*
   * A successful PING is a response packet, not
   * another request or a protocol error packet.
   */
  assert(response.type == PROTOCOL_TYPE_RESPONSE);

  /*
   * Preserve the fields identifying the transaction.
   * The master uses SEQ to match this response to
   * the request that it transmitted.
   */
  assert(response.destination == request.destination);
  assert(response.sequence == request.sequence);
  assert(response.command == DISPATCHER_COMMAND_PING);

  /*
   * Protocol v1 requires exactly 14 response bytes:
   * 12 UID bytes followed by firmware major and minor.
   */
  assert(response.payload_length == DISPATCHER_PING_RESPONSE_SIZE);

  /*
   * Compare all 12 UID bytes, including zero if present.
   * memcmp() checks binary data rather than treating the
   * UID as a null-terminated character string.
   */
  assert(memcmp(response.payload, uid, DISPATCHER_UID_SIZE) == 0);

  /*
   * Firmware version is encoded as two separate bytes.
   * There is no 16-bit endianness conversion.
   */
  assert(response.payload[12] == 2U);
  assert(response.payload[13] == 5U);
}

/*
 * Verify that rejected PING requests leave the caller's
 * response packet unchanged.
 *
 * WHAT:
 * Exercise wrong addresses, invalid packet types,
 * incorrect commands, nonempty PING payloads and
 * reserved addresses.
 *
 * WHY:
 * A malformed or unrelated request must never produce
 * a usable response packet.
 *
 * Preserving output storage on failure prevents callers
 * from accidentally treating a partially constructed
 * packet as a successful result.
 */
static void test_ping_rejected_requests(void) {
  const uint8_t uid[DISPATCHER_UID_SIZE] = {0};

  protocol_packet_t request = {.destination = 0x17U,
                               .sequence = 0x42U,
                               .type = PROTOCOL_TYPE_REQUEST,
                               .command = DISPATCHER_COMMAND_PING,
                               .payload_length = 0U};

  /*
   * Initialize the entire output packet to a known
   * byte pattern, including unused payload storage.
   *
   * This detects accidental writes even when the
   * visible response header appears unchanged.
   */
  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  /* A request for another device must remain silent. */
  request.destination = 0x18U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /*
   * Broadcast PING is forbidden. Multiple devices
   * replying to one broadcast could drive the shared
   * RS-485 bus simultaneously.
   */
  request.destination = 0x00U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Restore the valid unicast destination. */
  request.destination = 0x17U;

  /* Response packets must not execute as requests. */
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

  /* Another command must not be treated as PING. */
  request.command = 0x10U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.command = DISPATCHER_COMMAND_PING;

  /*
   * PING requires LEN=0. A CRC-valid packet can still
   * contain a command-specific invalid payload length.
   */
  request.payload_length = 1U;

  assert(!dispatcher_prepare_ping_response(&request, 0x17U, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.payload_length = 0U;

  /*
   * An uncommissioned device must not answer PING.
   * UID-based address assignment is handled separately.
   */
  assert(!dispatcher_prepare_ping_response(&request, 0xFFU, uid, 1U, 0U,
                                           &response));

  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Verify defensive argument validation.
 *
 * WHY:
 * Null pointers cannot provide valid packet or UID
 * storage. Rejecting them avoids undefined behavior.
 *
 * Request and response must also be separate objects,
 * because constructing the response in request storage
 * could overwrite fields still required for validation.
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
 * Verify that a constructed PING response survives
 * the complete protocol encode/decode round trip.
 *
 * WHAT:
 * Build a response, encode it with CRC and COBS,
 * then decode the resulting frame into another packet.
 *
 * WHY:
 * Testing only the in-memory response structure would
 * not verify that its fields and payload are compatible
 * with the existing protocol encoder and decoder.
 *
 * The wire buffer includes a trailing zero delimiter.
 * protocol_decode() expects the COBS body without it,
 * because the frame receiver normally removes that
 * delimiter before calling the decoder.
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
   * The protocol encoder adds the version byte,
   * CRC-16/CCITT-FALSE and COBS framing.
   *
   * The 34-byte buffer covers the largest permitted
   * protocol frame without dynamic allocation.
   */
  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  size_t wire_length = 0U;

  assert(protocol_encode(&response, wire, sizeof(wire), &wire_length) ==
         PROTOCOL_OK);

  assert(wire_length >= 2U);
  assert(wire_length <= sizeof(wire));
  assert(wire[wire_length - 1U] == 0U);

  /*
   * COBS guarantees that the frame delimiter appears
   * only at the end of the encoded frame.
   */
  for (size_t i = 0U; i < wire_length - 1U; ++i) {
    assert(wire[i] != 0U);
  }

  protocol_packet_t decoded = {0};

  assert(protocol_decode(wire, wire_length - 1U, &decoded) == PROTOCOL_OK);

  assert(decoded.destination == response.destination);
  assert(decoded.sequence == response.sequence);
  assert(decoded.type == PROTOCOL_TYPE_RESPONSE);
  assert(decoded.command == DISPATCHER_COMMAND_PING);
  assert(decoded.payload_length == DISPATCHER_PING_RESPONSE_SIZE);

  /*
   * The UID deliberately contains embedded zero bytes.
   * This verifies that COBS correctly preserves binary
   * payload data containing the frame delimiter value.
   */
  assert(memcmp(decoded.payload, uid, DISPATCHER_UID_SIZE) == 0);

  assert(decoded.payload[12] == 1U);
  assert(decoded.payload[13] == 2U);
}

/*
 * Run routing and PING response tests.
 *
 * WHAT:
 * Execute each test function and allow its assertions
 * to report failures to the host test runner.
 *
 * WHY:
 * Static test functions are not executed automatically.
 * Calling them explicitly ensures the PING implementation
 * is tested and avoids unused-function warnings with -Werror.
 *
 * AddressSanitizer and UndefinedBehaviorSanitizer provide
 * additional runtime checks during the host test execution.
 */
int main(void) {
  /* Verify packet-type filtering and RS-485 addressing rules. */
  test_unicast_routing();
  test_broadcast_routing();
  test_packet_types();
  test_null_packet();

  /* Verify PING response construction and protocol compatibility. */
  test_ping_response();
  test_ping_rejected_requests();
  test_ping_invalid_arguments();
  test_ping_wire_round_trip();

  return 0;
}
