
#include "commissioning.h"

#include "dispatcher.h"
#include "protocol.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Compile-time protocol compatibility checks.
 *
 * WHAT:
 * Ensure that the fixed commissioning payload fits
 * into the existing protocol packet storage.
 *
 * WHY:
 * _Static_assert is standard C11. It catches an
 * incompatible protocol-size change at build time,
 * before any firmware can access beyond the payload.
 *
 * It generates no machine code and uses no SRAM.
 */
_Static_assert(COMMISSIONING_UID_SIZE == 12U,
               "Protocol v1 requires a 12-byte UID");

_Static_assert(COMMISSIONING_PAYLOAD_SIZE == 13U,
               "ASSIGN_ADDRESS_BY_UID must contain 13 payload bytes");

_Static_assert(COMMISSIONING_PAYLOAD_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "Commissioning payload exceeds packet capacity");

bool commissioning_address_valid(uint8_t address) {
  /*
   * A uint8_t is restricted to the inclusive range
   * 0..255 by the standard integer representation
   * used on this target.
   *
   * Checking both boundaries excludes the two
   * reserved protocol addresses without needing
   * special-case control flow.
   */
  return address >= COMMISSIONING_MIN_ADDRESS &&
         address <= COMMISSIONING_MAX_ADDRESS;
}

bool commissioning_validate_assign_address_by_uid(
    const protocol_packet_t *request,
    const uint8_t local_uid[COMMISSIONING_UID_SIZE], uint8_t *new_address) {

  /*
   * Check pointers before any dereference.
   *
   * A missing packet, local identity or destination
   * for the validated address must not cause a read,
   * write or hardware side effect.
   */
  if (request == NULL || local_uid == NULL || new_address == NULL) {
    return false;
  }

  /*
   * Reuse the previously tested dispatcher routing.
   *
   * WHAT:
   * Accept only request packets routed as broadcast
   * ASSIGN_ADDRESS_BY_UID.
   *
   * WHY:
   * Commissioning must not be performed from unicast
   * packets, master-bound responses or error packets.
   *
   * The second argument is the current device address.
   * Broadcast routing does not depend on that address,
   * so the uncommissioned sentinel is appropriate here.
   *
   * This allows both uncommissioned and already
   * commissioned devices to validate UID-directed
   * broadcast assignments, as protocol v1 permits.
   */
  if (dispatcher_route_request(request, PROTOCOL_UNCOMMISSIONED_ADDRESS) !=
      DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID) {
    return false;
  }

  /*
   * Require exactly 12 UID bytes and one address.
   *
   * WHY:
   * A shorter payload might cause an out-of-bounds
   * read or incomplete UID matching.
   *
   * A longer payload has no defined meaning and
   * must not be accepted by silently ignoring bytes.
   *
   * Check length before reading any payload field.
   */
  if (request->payload_length != COMMISSIONING_PAYLOAD_SIZE) {
    return false;
  }

  /*
   * Validate the requested new address.
   *
   * WHAT:
   * Reject broadcast 0x00 and the uncommissioned
   * sentinel 0xFF.
   *
   * WHY:
   * A persistent record containing either address
   * could not identify a commissioned unicast device.
   *
   * This read is safe because the exact payload
   * length was validated above.
   */
  const uint8_t proposed_address =
      request->payload[COMMISSIONING_ADDRESS_OFFSET];

  if (!commissioning_address_valid(proposed_address)) {
    return false;
  }

  /*
   * Compare the complete 96-bit physical UID.
   *
   * WHAT:
   * XOR each received UID byte with the corresponding
   * local UID byte and accumulate all differences.
   *
   * WHY:
   * Any differing bit must prevent this device from
   * accepting the broadcast commissioning request.
   *
   * Accumulation ensures that all 12 UID positions
   * are examined instead of exiting at the first
   * mismatch. This also gives deterministic source
   * level work independent of the first mismatch.
   *
   * The loop bound is fixed at compile time, and
   * neither input array is modified.
   *
   * C integer promotion:
   * uint8_t operands are promoted to int for ^ and |.
   * Their values are always 0..255, so the bitwise
   * results remain representable in uint8_t.
   * The explicit cast documents the narrowing.
   *
   * This is identity matching, not cryptographic
   * authentication. Protocol v1 has no authentication.
   */
  uint8_t difference = 0U;

  for (size_t i = 0U; i < COMMISSIONING_UID_SIZE; ++i) {
    const uint8_t byte_difference =
        (uint8_t)(request->payload[i] ^ local_uid[i]);

    difference = (uint8_t)(difference | byte_difference);
  }

  if (difference != 0U) {
    return false;
  }

  /*
   * Publish the address only after all validation
   * conditions have succeeded.
   *
   * WHY:
   * The caller must never observe a partially
   * validated address or mistake a rejected packet
   * for a successful commissioning request.
   *
   * This write does not activate the address.
   * It merely provides a candidate for the later
   * EEPROM commit and readback-verification stage.
   */
  *new_address = proposed_address;

  return true;
}
