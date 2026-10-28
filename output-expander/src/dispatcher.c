#include "dispatcher.h"

#include <stddef.h>

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