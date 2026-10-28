#ifndef OUTPUT_EXPANDER_DISPATCHER_H
#define OUTPUT_EXPANDER_DISPATCHER_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Protocol v1 command identifiers implemented by this module.
 *
 * PING returns the physical MCU UID and firmware version.
 * GET_OUTPUTS returns the logical state of all eight outputs.
 * ASSIGN_UID is recognized for routing only; UID matching and
 * persistent address assignment are not implemented here.
 */
#define DISPATCHER_COMMAND_PING 0x01U
#define DISPATCHER_COMMAND_GET_OUTPUTS 0x11U
#define DISPATCHER_COMMAND_ASSIGN_UID 0x30U

/*
 * Fixed response payload sizes from protocol version 1.
 *
 * PING contains a 12-byte UID followed by two firmware
 * version bytes. GET_OUTPUTS contains one logical output mask.
 *
 * Both fit within PROTOCOL_MAX_PAYLOAD (24 bytes).
 */
#define DISPATCHER_UID_SIZE 12U
#define DISPATCHER_PING_RESPONSE_SIZE 14U
#define DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE 1U

/*
 * Results of the first command-dispatch stage.
 *
 * WHAT:
 * Classify an already decoded packet according to its
 * type, destination and command.
 *
 * WHY:
 * Every device on a multidrop RS-485 bus can receive the
 * same traffic. Only the addressed device may process
 * a unicast request.
 *
 * Broadcast ASSIGN_UID must be distinguished from unicast
 * requests because commissioning requires full UID matching
 * and must never generate a response.
 */
typedef enum {
  DISPATCHER_IGNORE = 0,
  DISPATCHER_UNICAST_REQUEST,
  DISPATCHER_BROADCAST_ASSIGN_UID
} dispatcher_route_t;

/*
 * Determine whether a request belongs to this device.
 *
 * The packet must already have passed COBS decoding,
 * packet structure validation and CRC verification.
 *
 * device_address == 0xFF indicates an uncommissioned device.
 *
 * This function does not execute commands, access hardware
 * or transmit RS-485 data.
 */
dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address);

/*
 * Construct a successful PING response.
 *
 * WHAT:
 * Validate a unicast PING request and construct a response
 * containing the MCU UID and firmware version.
 *
 * WHY:
 * Separating response construction from hardware access
 * allows the protocol behavior to be tested on the host.
 *
 * The caller supplies the UID in the protocol's required
 * byte order. Reading CH32V003 UID registers is the
 * responsibility of a future hardware-specific layer.
 *
 * The response preserves destination, sequence and command
 * so the master can match it with its outstanding request.
 *
 * Returns true on success.
 * Returns false for invalid arguments or requests.
 *
 * On failure, *response remains unchanged.
 * Request and response must not overlap.
 *
 * This function does not transmit or modify GPIO.
 */
bool dispatcher_prepare_ping_response(const protocol_packet_t *request,
                                      uint8_t device_address,
                                      const uint8_t uid[DISPATCHER_UID_SIZE],
                                      uint8_t firmware_major,
                                      uint8_t firmware_minor,
                                      protocol_packet_t *response);

/*
 * Construct a successful GET_OUTPUTS response.
 *
 * WHAT:
 * Validate a unicast GET_OUTPUTS request and construct
 * a response containing the current logical output mask.
 *
 * WHY:
 * The dispatcher must remain independent of the physical
 * GPIO mapping and active-low output polarity.
 *
 * output_mask:
 *   Bit 0 = output channel 1
 *   Bit 7 = output channel 8
 *   1 = logically ON
 *   0 = logically OFF
 *
 * For active-low hardware, logical ON corresponds to
 * a LOW electrical output level.
 *
 * The caller supplies the actual current logical state.
 *
 * Returns true on success.
 * Returns false for invalid arguments or requests.
 *
 * On failure, *response remains unchanged.
 * Request and response must not overlap.
 *
 * This function does not access GPIO, modify outputs
 * or transmit RS-485 data.
 */
bool dispatcher_prepare_get_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t output_mask,
                                             protocol_packet_t *response);

#endif
