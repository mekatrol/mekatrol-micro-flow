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

/*
 * Protocol v1 PING command.
 *
 * A successful response contains the MCU's 96-bit
 * unique identifier followed by two firmware
 * version bytes.
 *
 * The UID must always contain exactly 12 bytes.
 */
#define DISPATCHER_COMMAND_PING 0x01U
#define DISPATCHER_UID_SIZE 12U
#define DISPATCHER_PING_RESPONSE_SIZE 14U

/*
 * Construct a PING response without accessing hardware.
 *
 * WHAT:
 * Validate the request's routing, command and payload
 * length, then construct the complete response packet.
 *
 * WHY:
 * Keeping response construction separate from USART
 * transmission lets host tests verify protocol behavior
 * without requiring CH32V003 hardware.
 *
 * The caller provides the device UID in the agreed wire
 * byte order. Reading the MCU UID registers is handled
 * by a separate hardware module later.
 *
 * The firmware version is supplied explicitly so this
 * portable module does not depend on target build settings.
 *
 * Returns true only when the response is valid.
 * On failure, the response structure is unchanged.
 *
 * Request and response storage must not overlap.
 * Passing the same packet for both is unsupported.
 *
 * This function never transmits or modifies GPIO.
 */
bool dispatcher_prepare_ping_response(const protocol_packet_t *request,
                                      uint8_t device_address,
                                      const uint8_t uid[DISPATCHER_UID_SIZE],
                                      uint8_t firmware_major,
                                      uint8_t firmware_minor,
                                      protocol_packet_t *response);

#endif
