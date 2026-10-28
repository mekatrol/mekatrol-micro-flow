
#include "commissioning.h"

#include "dispatcher.h"
#include "protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Deterministic example MCU UID.
 *
 * WHAT:
 * Supply 12 distinct test bytes, including zero
 * and high-bit values.
 *
 * WHY:
 * Tests must detect incorrect byte ordering, short
 * comparisons and accidental signed-byte handling.
 *
 * These are fixture bytes, not a real device UID.
 */
static const uint8_t test_uid[COMMISSIONING_UID_SIZE] = {
    0x00U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9AU,
    0xBCU, 0xDEU, 0xF0U, 0x11U, 0x22U, 0x33U};

/*
 * Build one decoded broadcast ASSIGN_UID request.
 *
 * The framing layer normally produces this packet
 * only after validating COBS, version, length and CRC.
 *
 * This unit test operates at the command-validation
 * boundary, not at the raw USART-byte boundary.
 */
static protocol_packet_t make_assign_request(uint8_t new_address) {
  protocol_packet_t packet = {0};

  packet.destination = 0x00U;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.command = DISPATCHER_COMMAND_ASSIGN_UID;
  packet.payload_length = COMMISSIONING_PAYLOAD_SIZE;

  for (size_t i = 0U; i < COMMISSIONING_UID_SIZE; ++i) {
    packet.payload[i] = test_uid[i];
  }

  packet.payload[COMMISSIONING_ADDRESS_OFFSET] = new_address;

  return packet;
}

/*
 * Check the complete commissioned-address range.
 *
 * WHAT:
 * Verify every possible uint8_t address.
 *
 * WHY:
 * The valid range must have no accidental holes,
 * and reserved values must be rejected consistently.
 */
static void test_address_validation(void) {
  for (unsigned int address = 0U; address < 256U; ++address) {
    const bool expected = address >= COMMISSIONING_MIN_ADDRESS &&
                          address <= COMMISSIONING_MAX_ADDRESS;

    assert(commissioning_address_valid((uint8_t)address) == expected);
  }
}

/*
 * Verify every valid commissioned address.
 *
 * WHAT:
 * Exercise all addresses 0x01 through 0xFE in a
 * correctly formatted broadcast assignment.
 *
 * WHY:
 * Commissioning must not accidentally reserve
 * otherwise legal addresses or alter their value.
 *
 * The candidate output is reset for each request.
 */
static void test_all_valid_assignments(void) {
  for (unsigned int address = 1U; address <= 254U; ++address) {
    const protocol_packet_t request = make_assign_request((uint8_t)address);

    uint8_t candidate = 0xFFU;

    assert(commissioning_validate_assign_uid(&request, test_uid, &candidate));

    assert(candidate == (uint8_t)address);

    /*
     * Validation is read-only with respect to the
     * input packet; no active-address state exists
     * in this module.
     */
    assert(request.destination == 0x00U);
    assert(request.type == PROTOCOL_TYPE_REQUEST);
  }
}

/*
 * Verify a single incorrect bit in every UID byte.
 *
 * WHAT:
 * Flip each of the eight bits in each of the
 * twelve UID positions independently.
 *
 * WHY:
 * A truncated comparison or skipped UID byte
 * could allow a different device to accept
 * another device's commissioning request.
 *
 * That must be rejected without publishing an address.
 */
static void test_every_uid_bit_mismatch(void) {
  for (size_t byte_index = 0U; byte_index < COMMISSIONING_UID_SIZE;
       ++byte_index) {

    for (unsigned int bit_index = 0U; bit_index < 8U; ++bit_index) {

      protocol_packet_t request = make_assign_request(0x23U);

      request.payload[byte_index] =
          (uint8_t)(request.payload[byte_index] ^ (uint8_t)(1U << bit_index));

      uint8_t candidate = 0xA5U;

      assert(
          !commissioning_validate_assign_uid(&request, test_uid, &candidate));

      assert(candidate == 0xA5U);
    }
  }
}

/*
 * Check the two reserved address values.
 *
 * The UID matches, so rejection must specifically
 * be caused by the invalid address.
 */
static void test_reserved_addresses(void) {
  const uint8_t invalid_addresses[] = {0x00U, 0xFFU};

  for (size_t i = 0U;
       i < sizeof(invalid_addresses) / sizeof(invalid_addresses[0]); ++i) {

    const protocol_packet_t request = make_assign_request(invalid_addresses[i]);

    uint8_t candidate = 0xA5U;

    assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));

    assert(candidate == 0xA5U);
  }
}

/*
 * Verify exact payload-length enforcement.
 *
 * WHAT:
 * Test every representable length from 0 to 255.
 *
 * WHY:
 * Valid wire packets are limited to 24 payload bytes,
 * but direct in-memory callers could provide arbitrary
 * uint8_t lengths.
 *
 * The validation function must reject all incorrect
 * lengths before accessing the UID/address layout.
 */
static void test_all_payload_lengths(void) {
  for (unsigned int length = 0U; length < 256U; ++length) {
    protocol_packet_t request = make_assign_request(0x23U);

    request.payload_length = (uint8_t)length;

    uint8_t candidate = 0xA5U;

    const bool accepted =
        commissioning_validate_assign_uid(&request, test_uid, &candidate);

    if (length == COMMISSIONING_PAYLOAD_SIZE) {
      assert(accepted);
      assert(candidate == 0x23U);
    } else {
      assert(!accepted);
      assert(candidate == 0xA5U);
    }
  }
}

/*
 * Verify only the broadcast request form is accepted.
 *
 * WHAT:
 * Test unicast destinations, reserved destination,
 * ordinary response and error packet types, and
 * a different command identifier.
 *
 * WHY:
 * Commissioning must never be triggered by unrelated
 * traffic on a multidrop bus.
 */
static void test_invalid_routing(void) {
  protocol_packet_t request = make_assign_request(0x23U);
  uint8_t candidate = 0xA5U;

  request.destination = 0x23U;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));
  assert(candidate == 0xA5U);

  request.destination = 0xFFU;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));
  assert(candidate == 0xA5U);

  request.destination = 0x00U;
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));
  assert(candidate == 0xA5U);

  request.type = PROTOCOL_TYPE_ERROR;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));
  assert(candidate == 0xA5U);

  request.type = PROTOCOL_TYPE_REQUEST;
  request.command = DISPATCHER_COMMAND_PING;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));
  assert(candidate == 0xA5U);
}

/*
 * Verify NULL arguments are safely rejected.
 *
 * WHAT:
 * Test missing packet storage, local UID storage
 * and result storage separately.
 *
 * WHY:
 * Invalid pointers must never cause a read or write
 * and must never be interpreted as a valid assignment.
 */
static void test_invalid_arguments(void) {
  const protocol_packet_t request = make_assign_request(0x23U);

  uint8_t candidate = 0xA5U;

  assert(!commissioning_validate_assign_uid(NULL, test_uid, &candidate));

  assert(!commissioning_validate_assign_uid(&request, NULL, &candidate));

  assert(!commissioning_validate_assign_uid(&request, test_uid, NULL));

  assert(candidate == 0xA5U);
}

/*
 * Verify a matching UID remains valid even when the
 * proposed address equals the device's old address.
 *
 * WHY:
 * The protocol permits readdressing an already
 * commissioned device, and repeated configuration
 * commands must be idempotent.
 *
 * This module does not know the old address. That is
 * deliberate: avoiding unnecessary EEPROM writes for
 * an unchanged address is the responsibility of the
 * later persistent-configuration layer.
 */
static void test_repeated_assignment(void) {
  const protocol_packet_t request = make_assign_request(0x23U);

  uint8_t first_candidate = 0xFFU;
  uint8_t second_candidate = 0xFFU;

  assert(
      commissioning_validate_assign_uid(&request, test_uid, &first_candidate));

  assert(
      commissioning_validate_assign_uid(&request, test_uid, &second_candidate));

  assert(first_candidate == 0x23U);
  assert(second_candidate == first_candidate);
}

/*
 * Verify UID bytes retain their exact ordering.
 *
 * WHAT:
 * Supply the correct bytes in reverse order.
 *
 * WHY:
 * A commissioning implementation must never
 * reinterpret the MCU UID as an integer or change
 * byte ordering based on host/target architecture.
 */
static void test_uid_byte_order(void) {
  protocol_packet_t request = make_assign_request(0x23U);

  for (size_t i = 0U; i < COMMISSIONING_UID_SIZE; ++i) {
    request.payload[i] = test_uid[COMMISSIONING_UID_SIZE - 1U - i];
  }

  uint8_t candidate = 0xA5U;

  assert(!commissioning_validate_assign_uid(&request, test_uid, &candidate));

  assert(candidate == 0xA5U);
}

/*
 * Verify the input packet and UID remain unchanged.
 *
 * WHAT:
 * Preserve copies before validation and compare them
 * afterward.
 *
 * WHY:
 * Pure command validation must not rewrite the request
 * or accidentally modify the hardware-provided identity.
 *
 * This also ensures that a later EEPROM layer can use
 * the same validated request for diagnostics if needed.
 */
static void test_inputs_unchanged(void) {
  protocol_packet_t request = make_assign_request(0x23U);
  const protocol_packet_t original_request = request;

  uint8_t local_uid[COMMISSIONING_UID_SIZE];
  uint8_t original_uid[COMMISSIONING_UID_SIZE];

  memcpy(local_uid, test_uid, sizeof(local_uid));
  memcpy(original_uid, test_uid, sizeof(original_uid));

  uint8_t candidate = 0xA5U;

  assert(commissioning_validate_assign_uid(&request, local_uid, &candidate));

  assert(candidate == 0x23U);
  assert(memcmp(&request, &original_request, sizeof(request)) == 0);

  assert(memcmp(local_uid, original_uid, sizeof(local_uid)) == 0);
}

/*
 * Run the complete portable commissioning test suite.
 *
 * No EEPROM, USART, MCU UID registers, GPIO or actual
 * RS-485 traffic is required for these tests.
 *
 * The existing host-debug configuration provides
 * AddressSanitizer and UndefinedBehaviorSanitizer.
 */
int main(void) {
  test_address_validation();
  test_all_valid_assignments();
  test_every_uid_bit_mismatch();
  test_reserved_addresses();
  test_all_payload_lengths();
  test_invalid_routing();
  test_invalid_arguments();
  test_repeated_assignment();
  test_uid_byte_order();
  test_inputs_unchanged();

  return 0;
}
