
#include "dispatcher.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Verify protocol buffer capacity at compile time.
 *
 * WHY:
 * A future protocol change must not allow any response
 * builder to write beyond the fixed payload array.
 *
 * _Static_assert is supported by standard C11 and
 * produces no runtime code or memory overhead.
 */
_Static_assert(DISPATCHER_PING_RESPONSE_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "PING response exceeds payload capacity");

_Static_assert(DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "GET_OUTPUTS response exceeds payload capacity");

_Static_assert(DISPATCHER_SET_OUTPUTS_RESPONSE_SIZE <= PROTOCOL_MAX_PAYLOAD,
               "SET_OUTPUTS response exceeds payload capacity");

dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address) {

  /*
   * Only requests may advance to command processing.
   *
   * Responses and error packets are master-bound traffic.
   * An expander must never execute them as commands.
   *
   * NULL is checked first to prevent invalid access.
   */
  if (packet == NULL || packet->type != PROTOCOL_TYPE_REQUEST) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Process broadcast routing before unicast filtering.
   *
   * An uncommissioned device has no valid unicast address
   * but must still recognize broadcast ASSIGN_ADDRESS_BY_UID.
   *
   * Other broadcast commands are silently discarded. The discovery commands
   * below are only classified here; the application schedules their replies
   * so multiple devices do not begin transmitting together.
   *
   * UID matching and persistent address assignment
   * are separate operations not performed here.
   */
  if (packet->destination == PROTOCOL_BROADCAST_ADDRESS) {
    if (packet->command == DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID) {
      return DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID;
    }

    /*
     * Device discovery is deliberately routed as a special broadcast rather
     * than as an ordinary command. Every expander must hear the request even
     * when its configured address is unknown to the master, while the
     * communications application must still apply a randomized delay before
     * it is allowed to answer. The matching cancel command is routed through
     * the same special path so a master can promptly silence pending replies.
     */
    if (packet->command == DISPATCHER_COMMAND_DISCOVER_DEVICES) {
      return DISPATCHER_BROADCAST_DISCOVER_DEVICES;
    }

    if (packet->command == DISPATCHER_COMMAND_CANCEL_DISCOVERY) {
      return DISPATCHER_BROADCAST_CANCEL_DISCOVERY;
    }

    return DISPATCHER_IGNORE;
  }

  /* Permit isolated-device UID discovery before commissioning. */
  if (device_address == PROTOCOL_UNCOMMISSIONED_ADDRESS &&
      packet->destination == PROTOCOL_UNCOMMISSIONED_ADDRESS &&
      packet->command == DISPATCHER_COMMAND_PING) {
    return DISPATCHER_UNICAST_REQUEST;
  }

  /* Reject every other use of the reserved addresses. */
  if (device_address == PROTOCOL_UNCOMMISSIONED_ADDRESS ||
      device_address == PROTOCOL_BROADCAST_ADDRESS ||
      packet->destination == PROTOCOL_UNCOMMISSIONED_ADDRESS) {
    return DISPATCHER_IGNORE;
  }

  /*
   * Only the addressed device can execute the request.
   *
   * Ignore requests for other devices without transmitting
   * an error or causing any hardware side effects.
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
   * Reject invalid pointers before accessing their contents.
   *
   * Input and output packets must be separate objects
   * to avoid overwriting request fields during response
   * construction.
   */
  if (request == NULL || uid == NULL || response == NULL ||
      request == response) {
    return false;
  }

  /*
   * Only correctly addressed unicast requests may
   * generate a PING response.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * PING requires command 0x01 and an empty payload.
   *
   * COBS and CRC validation alone do not guarantee
   * command-specific payload validity.
   */
  if (request->command != DISPATCHER_COMMAND_PING ||
      request->payload_length != 0U) {
    return false;
  }

  /*
   * Construct the response in local stack storage.
   *
   * Zero-initialization prevents uninitialized payload
   * bytes from being retained in the completed object.
   *
   * The caller's response is only updated on success.
   */
  protocol_packet_t prepared = {0};

  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = DISPATCHER_COMMAND_PING;
  prepared.payload_length = DISPATCHER_PING_RESPONSE_SIZE;

  /*
   * Copy the complete physical MCU UID.
   *
   * The hardware-specific layer must supply the same
   * byte ordering defined by the protocol contract.
   *
   * A bounded loop avoids requiring libc memcpy().
   */
  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    prepared.payload[i] = uid[i];
  }

  /*
   * Firmware version occupies two separate bytes.
   *
   * No endianness conversion is required because
   * neither field is a multi-byte integer.
   */
  prepared.payload[DISPATCHER_UID_SIZE] = firmware_major;
  prepared.payload[DISPATCHER_UID_SIZE + 1U] = firmware_minor;

  /*
   * Publish the complete packet.
   *
   * The foreground application will later encode
   * and transmit it after hardware integration.
   */
  *response = prepared;

  return true;
}

bool dispatcher_prepare_get_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t output_mask,
                                             protocol_packet_t *response) {

  /*
   * Prevent invalid pointers or in-place overwriting
   * of the incoming request packet.
   */
  if (request == NULL || response == NULL || request == response) {
    return false;
  }

  /*
   * GET_OUTPUTS is a unicast-only command.
   *
   * Broadcast reads would cause multiple expanders
   * to drive the shared RS-485 bus simultaneously.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * A valid GET_OUTPUTS request has no payload.
   *
   * Reject any nonzero declared length even if the
   * frame has otherwise valid CRC and COBS encoding.
   */
  if (request->command != DISPATCHER_COMMAND_GET_OUTPUTS ||
      request->payload_length != 0U) {
    return false;
  }

  protocol_packet_t prepared = {0};

  /*
   * Preserve request identity so the master can
   * match the response to its outstanding transaction.
   */
  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = DISPATCHER_COMMAND_GET_OUTPUTS;

  /*
   * Return exactly one byte of logical output state.
   *
   * A set bit means ON although the physical GPIO
   * is LOW on this active-low output hardware.
   *
   * The dispatcher never inverts the mask.
   */
  prepared.payload_length = DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE;
  prepared.payload[0] = output_mask;

  *response = prepared;

  return true;
}

bool dispatcher_parse_set_outputs(const protocol_packet_t *request,
                                  uint8_t device_address,
                                  uint8_t *requested_mask) {

  /*
   * Validate pointer arguments before reading the packet.
   *
   * On failure, the caller's requested_mask remains
   * unchanged so invalid input cannot accidentally
   * become a new output state.
   */
  if (request == NULL || requested_mask == NULL) {
    return false;
  }

  /*
   * SET_OUTPUTS must be a valid addressed unicast request.
   *
   * Broadcast writes are intentionally prohibited because
   * they could change every expander's outputs at once.
   *
   * Uncommissioned devices must not execute the command.
   */
  if (dispatcher_route_request(request, device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return false;
  }

  /*
   * SET_OUTPUTS requires exactly one payload byte.
   *
   * This is important because malformed request lengths
   * must not cause partial output changes.
   *
   * All eight output channels are represented by that
   * one byte, so every possible uint8_t mask is valid.
   */
  if (request->command != DISPATCHER_COMMAND_SET_OUTPUTS ||
      request->payload_length != 1U) {
    return false;
  }

  /*
   * Publish the requested LOGICAL output state.
   *
   * Bit 0 represents output channel 1.
   * Bit 7 represents output channel 8.
   *
   * Do not invert the value for active-low GPIO here.
   * Hardware polarity conversion belongs in the HAL.
   *
   * The foreground application must apply this mask
   * to hardware before acknowledging success.
   */
  *requested_mask = request->payload[0];

  return true;
}

bool dispatcher_prepare_set_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t accepted_mask,
                                             protocol_packet_t *response) {

  /*
   * Validate pointers and prohibit using the same
   * packet object for input and output.
   */
  if (request == NULL || response == NULL || request == response) {
    return false;
  }

  /*
   * Revalidate the original SET_OUTPUTS request.
   *
   * WHY:
   * A success response must not be prepared for a
   * malformed command or a request addressed to
   * another device.
   *
   * This function assumes the hardware application
   * has already succeeded; it does not verify GPIO.
   */
  uint8_t requested_mask = 0U;

  if (!dispatcher_parse_set_outputs(request, device_address, &requested_mask)) {
    return false;
  }

  /*
   * A successful asynchronous request acknowledges the exact target stored by
   * the sequencer. It is not compared with GPIO state because later channels
   * may remain safely pending for several seconds.
   */
  if (accepted_mask != requested_mask) {
    return false;
  }

  /*
   * Construct the success response in private storage.
   *
   * No packet is published until all validation passes.
   */
  protocol_packet_t prepared = {0};

  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_RESPONSE;
  prepared.command = DISPATCHER_COMMAND_SET_OUTPUTS;

  /*
   * SET_OUTPUTS success payload:
   *   LEN = 1
   *   PAYLOAD[0] = applied logical output mask.
   *
   * This is not an electrical GPIO port value.
   * Active-low conversion remains the HAL's job.
   */
  prepared.payload_length = DISPATCHER_SET_OUTPUTS_RESPONSE_SIZE;
  prepared.payload[0] = accepted_mask;

  /*
   * Publish the completed response.
   *
   * This function does not change outputs, write
   * persistent configuration or enable RS-485 TX.
   */
  *response = prepared;

  return true;
}
