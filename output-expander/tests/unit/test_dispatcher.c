#include "dispatcher.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

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
 * Run routing cases independently.
 *
 * assert() terminates the test process on failure,
 * allowing CTest to report a failing test.
 *
 * The existing host build enables AddressSanitizer
 * and UndefinedBehaviorSanitizer to detect additional
 * memory and undefined-behavior problems.
 */
int main(void) {
  test_unicast_routing();
  test_broadcast_routing();
  test_packet_types();
  test_null_packet();

  return 0;
}