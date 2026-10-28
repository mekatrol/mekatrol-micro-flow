#ifndef OUTPUT_EXPANDER_DISPATCHER_H
#define OUTPUT_EXPANDER_DISPATCHER_H

#include "protocol.h"

#include <stdint.h>

/*
 * Results of the first command-dispatch stage.
 *
 * WHAT:
 * Classifies a fully decoded protocol packet according to
 * its packet type and destination address.
 *
 * WHY:
 * Every device on an RS-485 multidrop bus receives the
 * same electrical traffic. Only the addressed device
 * may process a unicast request.
 *
 * Broadcast commissioning is handled separately because
 * it requires UID matching and must never generate a reply.
 *
 * This stage does not execute commands or access hardware.
 */
typedef enum {
  DISPATCHER_IGNORE = 0,
  DISPATCHER_UNICAST_REQUEST,
  DISPATCHER_BROADCAST_ASSIGN_UID
} dispatcher_route_t;

/*
 * Determine whether a decoded request is intended
 * for this device.
 *
 * WHAT:
 * Examines the request type, destination and the device's
 * currently assigned address.
 *
 * WHY:
 * Address filtering must occur before command execution
 * to prevent unrelated RS-485 traffic from changing
 * device state or producing unwanted responses.
 *
 * The caller must supply a packet that has already passed
 * COBS decoding, packet validation and CRC verification.
 *
 * device_address == 0xFF indicates an uncommissioned device.
 *
 * This function has no hardware side effects.
 */
dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address);

#endif