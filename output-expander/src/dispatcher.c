#include "dispatcher.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Protocol v1 addressing constants.
 *
 * 0x00 addresses all devices on the RS-485 bus.
 * 0xFF is reserved for devices without an assigned address.
 *
 * Neither value is a valid commissioned unicast address.
 */
#define DISPATCHER_BROADCAST_ADDRESS 0x00U
#define DISPATCHER_UNCOMMISSIONED_ADDRESS 0xFFU

/*
 * ASSIGN_UID is the only command permitted on broadcast
 * in protocol version 1.
 *
 * A later dispatch stage must compare the complete 12-byte
 * UID and verify persistent storage before changing address.
 */
#define DISPATCHER_COMMAND_ASSIGN_UID 0x30U

dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address) {

  /*
   * A null pointer cannot contain a valid request.
   *
   * Response and error packets are intended for the master,
   * not for execution by an output expander.
   *
   * Ignoring them also avoids devices accidentally reacting
   * to one another's responses on a shared RS-485 bus.
   */
  if (packet == NULL || packet->type != PROTOCOL_TYPE_REQUEST) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Broadcast traffic is evaluated before unicast routing.
   *
   * ASSIGN_UID must be received even by an uncommissioned
   * device, whose internal address is still 0xFF.
   *
   * No other v1 command is broadcast-capable.
   *
   * Returning BROADCAST_ASSIGN_UID does not authorize an
   * address change: UID and payload validation come later.
   */
  if (packet->destination == DISPATCHER_BROADCAST_ADDRESS) {
    if (packet->command == DISPATCHER_COMMAND_ASSIGN_UID) {
      return DISPATCHER_BROADCAST_ASSIGN_UID;
    }

    return DISPATCHER_IGNORE;
  }

  /*
   * A device with address 0xFF is not commissioned and
   * must not accept unicast requests.
   *
   * Address 0x00 is reserved for broadcast and cannot
   * identify an individual device.
   *
   * Packets using destination 0xFF are invalid as unicast
   * requests even when the receiving device is uncommissioned.
   */
  if (device_address == DISPATCHER_UNCOMMISSIONED_ADDRESS ||
      device_address == DISPATCHER_BROADCAST_ADDRESS ||
      packet->destination == DISPATCHER_UNCOMMISSIONED_ADDRESS) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Only a request addressed to this commissioned device
   * may advance to command execution.
   *
   * Wrong-address packets are silently discarded rather
   * than answered, preventing RS-485 bus contention.
   */
  if (packet->destination == device_address) {
    return DISPATCHER_UNICAST_REQUEST;
  }

  return DISPATCHER_IGNORE;
}

/*
 * Construct a protocol v1 PING success response.
 *
 * WHAT:
 * Create a response packet containing the device UID
 * and firmware version, while preserving the request's
 * destination, sequence number and command.
 *
 * WHY:
 * The master uses these copied fields to associate a
 * response with its outstanding transaction.
 *
 * The complete packet is constructed in temporary
 * storage and published only after validation.
 * An invalid request must not modify the caller's
 * response structure.
 */
bool dispatcher_prepare_ping_response(const protocol_packet_t *request,
                                      uint8_t device_address,
                                      const uint8_t uid[DISPATCHER_UID_SIZE],
                                      uint8_t firmware_major,
                                      uint8_t firmware_minor,
                                      protocol_packet_t *response) {

  /*
   * Validate pointer arguments before reading any data.
   *
   * The UID is required because the v1 PING response
   * identifies the physical MCU using all 12 UID bytes.
   *
   * Request and response must use different storage:
   * writing the response must not destroy the request
   * while its fields are still needed.
   */
  if (request == NULL || uid == NULL || response == NULL ||
      request == response) {
    return false;
  }

  /*
   * Reuse the existing address-filtering function.
   *
   * Only an eligible unicast request may generate a
   * response. Broadcasts, other device addresses,
   * and response/error packets must remain silent.
   *
   * This is particularly important on a shared
   * half-duplex RS-485 bus, where simultaneous
   * transmissions cause electrical contention.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * PING uses command 0x01 and has no request payload.
   *
   * A nonzero payload length is invalid even when the
   * packet has passed COBS decoding and CRC checking.
   *
   * Protocol framing validation does not replace
   * command-specific payload validation.
   */
  if (request->command != DISPATCHER_COMMAND_PING ||
      request->payload_length != 0U) {
    return false;
  }

  /*
   * Initialize the complete temporary packet.
   *
   * WHAT:
   * Set unused payload bytes to zero and populate
   * the response header.
   *
   * WHY:
   * Avoid reading uninitialized structure contents
   * in host tests or future protocol-processing code.
   *
   * This does not require heap allocation.
   */
  protocol_packet_t prepared = {0};

  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = request->command;
  prepared.payload_length = DISPATCHER_PING_RESPONSE_SIZE;

  /*
   * Copy the physical device's 96-bit UID.
   *
   * The exact UID byte order is supplied by the
   * hardware layer and must match the Rust protocol.
   *
   * Copying individual bytes avoids a dependency
   * on libc's memcpy().
   */
  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    prepared.payload[i] = uid[i];
  }

  /*
   * Append firmware version bytes after the UID.
   *
   * Payload layout:
   *
   *   [0..11] = UID
   *   [12]    = firmware major version
   *   [13]    = firmware minor version
   *
   * These are separate u8 fields, not a 16-bit
   * integer, so endianness conversion is unnecessary.
   */
  prepared.payload[DISPATCHER_UID_SIZE] = firmware_major;
  prepared.payload[DISPATCHER_UID_SIZE + 1U] = firmware_minor;

  /*
   * Publish the completed response only after all
   * request checks and payload construction succeed.
   *
   * The caller may subsequently encode this packet
   * using protocol_encode(), which adds the version,
   * CRC, COBS framing and final zero delimiter.
   *
   * No USART transmission occurs here.
   */
  *response = prepared;

  return true;
}
