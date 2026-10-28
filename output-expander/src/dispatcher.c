#include "dispatcher.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Address 0x00 identifies a broadcast request.
 *
 * Address 0xFF represents the internal uncommissioned
 * state and is never a valid unicast destination.
 *
 * Commissioned devices use addresses 0x01 through 0xFE.
 */
#define DISPATCHER_BROADCAST_ADDRESS 0x00U
#define DISPATCHER_UNCOMMISSIONED_ADDRESS 0xFFU

/*
 * Verify at compile time that the fixed response payloads
 * fit inside the existing protocol packet structure.
 *
 * WHY:
 * Future changes to protocol capacity must not silently
 * introduce an out-of-bounds payload write.
 */
_Static_assert(DISPATCHER_PING_RESPONSE_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "PING response exceeds protocol payload capacity");

_Static_assert(DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "GET_OUTPUTS response exceeds protocol payload capacity");

dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address) {

  /*
   * Only request packets may reach the command dispatcher.
   *
   * Responses and errors are master-bound traffic. Ignoring
   * them prevents devices from interpreting another device's
   * response as a command on the shared RS-485 bus.
   *
   * Check for NULL before accessing packet fields.
   */
  if (packet == NULL || packet->type != PROTOCOL_TYPE_REQUEST) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Handle broadcast traffic before unicast address filtering.
   *
   * An uncommissioned device must still be able to receive
   * ASSIGN_UID even though it has no assigned unicast address.
   *
   * Protocol v1 permits only ASSIGN_UID on broadcast.
   * All other broadcast commands are silently ignored.
   *
   * This result only indicates routing eligibility.
   * UID validation and EEPROM persistence must occur later.
   *
   * Broadcast commissioning must never produce a response.
   */
  if (packet->destination == DISPATCHER_BROADCAST_ADDRESS) {
    if (packet->command == DISPATCHER_COMMAND_ASSIGN_UID) {
      return DISPATCHER_BROADCAST_ASSIGN_UID;
    }

    return DISPATCHER_IGNORE;
  }

  /*
   * Reject reserved and uncommissioned unicast addresses.
   *
   * 0xFF is not a usable destination, even when the device
   * itself is uncommissioned.
   *
   * 0x00 cannot represent an individual device because
   * it is reserved for broadcast traffic.
   */
  if (device_address == DISPATCHER_UNCOMMISSIONED_ADDRESS ||
      device_address == DISPATCHER_BROADCAST_ADDRESS ||
      packet->destination == DISPATCHER_UNCOMMISSIONED_ADDRESS) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Only requests matching this device's commissioned
   * address may advance to command-specific processing.
   *
   * Requests for other devices are discarded silently
   * to avoid unwanted execution and RS-485 contention.
   */
  if (packet->destination == device_address) {
    return DISPATCHER_UNICAST_REQUEST;
  }

  return DISPATCHER_IGNORE;
}

bool dispatcher_prepare_ping_response(const protocol_packet_t *request,
                                      uint8_t device_address,
                                      const uint8_t uid[DISPATCHER_UID_SIZE],
                                      uint8_t firmware_major,
                                      uint8_t firmware_minor,
                                      protocol_packet_t *response) {

  /*
   * Validate pointers before accessing packet or UID data.
   *
   * Separate request and response storage is required
   * because response construction overwrites packet fields.
   */
  if (request == NULL || uid == NULL || response == NULL ||
      request == response) {
    return false;
  }

  /*
   * Reuse the common address-filtering rules.
   *
   * Only correctly addressed unicast requests may generate
   * a PING response. Broadcasts, uncommissioned addresses,
   * responses and error packets must remain silent.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * PING command 0x01 requires an empty request payload.
   *
   * A frame can have valid COBS encoding and CRC while still
   * containing an invalid command-specific payload length.
   *
   * Reject these requests before constructing a response.
   */
  if (request->command != DISPATCHER_COMMAND_PING ||
      request->payload_length != 0U) {
    return false;
  }

  /*
   * Build the entire response in temporary stack storage.
   *
   * Zero-initialization gives deterministic values to
   * unused payload bytes without allocating heap memory.
   *
   * The caller's response is not modified until every
   * validation check has succeeded.
   */
  protocol_packet_t prepared = {0};

  /*
   * Preserve the fields needed by the master to associate
   * this response with the original request.
   */
  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = DISPATCHER_COMMAND_PING;
  prepared.payload_length = DISPATCHER_PING_RESPONSE_SIZE;

  /*
   * Copy the complete 96-bit MCU unique identifier.
   *
   * The hardware layer supplies its required wire byte order.
   * A byte loop avoids needing libc memcpy() in this portable
   * protocol module and limits copying to exactly 12 bytes.
   */
  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    prepared.payload[i] = uid[i];
  }

  /*
   * Append two separate firmware version bytes.
   *
   * Payload layout:
   *   [0..11] = UID
   *   [12]    = firmware major
   *   [13]    = firmware minor
   *
   * Since these are separate bytes, no endian conversion
   * is necessary.
   */
  prepared.payload[DISPATCHER_UID_SIZE] = firmware_major;
  prepared.payload[DISPATCHER_UID_SIZE + 1U] = firmware_minor;

  /*
   * Publish the completed packet.
   *
   * A future foreground dispatcher will pass this packet
   * to protocol_encode() and then to the RS-485 transmitter.
   *
   * There is no hardware access or transmission here.
   */
  *response = prepared;

  return true;
}

bool dispatcher_prepare_get_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t output_mask,
                                             protocol_packet_t *response) {

  /*
   * Reject invalid pointers and overlapping packet objects.
   *
   * Keeping input and output separate prevents the response
   * write from destroying request information still in use.
   */
  if (request == NULL || response == NULL || request == response) {
    return false;
  }

  /*
   * A GET_OUTPUTS response is permitted only for a valid
   * unicast request addressed to this commissioned device.
   *
   * Broadcast GET_OUTPUTS must not generate responses:
   * multiple devices replying would cause RS-485 contention.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * GET_OUTPUTS command 0x11 has no request payload.
   *
   * Checking the command and declared payload length
   * prevents CRC-valid but semantically invalid requests
   * from being treated as successful commands.
   */
  if (request->command != DISPATCHER_COMMAND_GET_OUTPUTS ||
      request->payload_length != 0U) {
    return false;
  }

  /*
   * Construct a complete response in local storage.
   *
   * This guarantees that a rejected request cannot leave
   * a partially constructed packet in the caller's buffer.
   */
  protocol_packet_t prepared = {0};

  /*
   * Copy the request identity and identify the packet
   * as a successful response.
   */
  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  /*
   * The response contains exactly one logical output mask.
   *
   * Bit 0 represents channel 1; bit 7 represents channel 8.
   * A set bit means ON even though the physical output pin
   * is LOW on this active-low hardware.
   *
   * Do not invert the mask here: GPIO polarity belongs
   * to the hardware abstraction layer.
   */
  prepared.payload_length = DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE;
  prepared.payload[0] = output_mask;

  /*
   * Publish only the completed response.
   *
   * The function does not read GPIO registers, change
   * output states or transmit on RS-485.
   */
  *response = prepared;

  return true;
}
