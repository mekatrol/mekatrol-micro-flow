
#ifndef OUTPUT_EXPANDER_DISPATCHER_H
#define OUTPUT_EXPANDER_DISPATCHER_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Protocol version 1 command identifiers.
 *
 * Keep these values consistent with docs/protocol.md
 * and the protocol contract.
 *
 * Broadcast commissioning and discovery commands are recognized for routing
 * only. UID comparison, randomized reply scheduling, cancellation and EEPROM
 * persistence belong to the foreground communications application; keeping
 * those side effects out of this portable classifier makes it impossible for
 * routing alone to transmit or alter an address.
 */
#define DISPATCHER_COMMAND_PING 0x01U
#define DISPATCHER_COMMAND_SET_OUTPUTS 0x10U
#define DISPATCHER_COMMAND_GET_OUTPUTS 0x11U
#define DISPATCHER_COMMAND_GET_OUTPUT_DELAY 0x12U
#define DISPATCHER_COMMAND_SET_OUTPUT_DELAY 0x13U
#define DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID 0x30U
#define DISPATCHER_COMMAND_DISCOVER_DEVICES 0x31U
#define DISPATCHER_COMMAND_CANCEL_DISCOVERY 0x32U

/*
 * Fixed protocol response sizes.
 *
 * PING:
 *   12 bytes UID + major version + minor version.
 *
 * GET_OUTPUTS and SET_OUTPUTS:
 *   One byte containing the logical output mask.
 *
 * All fit inside the protocol's 24-byte payload limit.
 */
#define DISPATCHER_UID_SIZE 12U
#define DISPATCHER_PING_RESPONSE_SIZE 14U
#define DISPATCHER_GET_OUTPUTS_RESPONSE_SIZE 1U
#define DISPATCHER_SET_OUTPUTS_RESPONSE_SIZE 1U

/*
 * Request routing classifications.
 *
 * WHAT:
 * Separate ignored traffic, addressed unicast requests, broadcast UID
 * commissioning, and the two discovery-control messages.
 *
 * WHY:
 * All devices on an RS-485 multidrop bus may receive
 * the same traffic, but only the addressed device may
 * execute a unicast command or transmit a response.
 *
 * Broadcast commissioning is processed after UID validation but never
 * replies. Discovery is also special: its route arms or cancels a delayed
 * foreground reply instead of transmitting directly from packet handling.
 */
typedef enum {
  DISPATCHER_IGNORE = 0,
  DISPATCHER_UNICAST_REQUEST,
  DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID,
  DISPATCHER_BROADCAST_DISCOVER_DEVICES,
  DISPATCHER_BROADCAST_CANCEL_DISCOVERY
} dispatcher_route_t;

/*
 * Classify a previously decoded and CRC-validated packet.
 *
 * device_address:
 *   0x01..0xFE = commissioned device
 *   0xFF       = uncommissioned device
 *
 * This function has no hardware side effects.
 */
dispatcher_route_t dispatcher_route_request(const protocol_packet_t *packet,
                                            uint8_t device_address);

/*
 * Construct a successful PING response.
 *
 * The UID must be supplied in the exact byte order
 * defined by the protocol contract.
 *
 * This portable function does not read MCU registers.
 *
 * On success, response contains the completed packet.
 * On failure, response remains unchanged.
 *
 * Request and response must occupy separate storage.
 */
bool dispatcher_prepare_ping_response(const protocol_packet_t *request,
                                      uint8_t device_address,
                                      const uint8_t uid[DISPATCHER_UID_SIZE],
                                      uint8_t firmware_major,
                                      uint8_t firmware_minor,
                                      protocol_packet_t *response);

/*
 * Construct a GET_OUTPUTS success response.
 *
 * output_mask is the current LOGICAL state:
 *
 *   Bit 0 = channel 1
 *   Bit 7 = channel 8
 *   1 = ON
 *   0 = OFF
 *
 * The physical output pins are active-low, but the
 * dispatcher must not invert the protocol mask.
 *
 * No GPIO registers are read by this function.
 */
bool dispatcher_prepare_get_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t output_mask,
                                             protocol_packet_t *response);

/*
 * Validate SET_OUTPUTS and extract its requested mask.
 *
 * WHAT:
 * Verify that this is a correctly addressed unicast
 * SET_OUTPUTS request with exactly one payload byte.
 *
 * WHY:
 * A valid CRC does not establish that a particular
 * command has a valid payload length.
 *
 * The application must validate the command BEFORE
 * modifying any GPIO outputs.
 *
 * On success, *requested_mask receives the logical
 * output mask from the request.
 *
 * On failure, *requested_mask remains unchanged.
 *
 * This function does NOT:
 *   - Change output GPIO states.
 *   - Update EEPROM.
 *   - Modify the communications timeout.
 *   - Generate or transmit a response.
 *
 * These operations belong to the foreground
 * application after request validation succeeds.
 */
bool dispatcher_parse_set_outputs(const protocol_packet_t *request,
                                  uint8_t device_address,
                                  uint8_t *requested_mask);

/*
 * Construct SET_OUTPUTS acknowledgment AFTER application.
 *
 * WHAT:
 * Validate the original request and construct a response
 * containing the logical target accepted by the sequencer.
 *
 * WHY:
 * Activation is asynchronous, so success confirms that the target was stored.
 * GET_OUTPUTS separately reports the physically applied mask while sequencing.
 *
 * Calling this function does not apply GPIO outputs.
 * The foreground must first execute the output change,
 * determine the resulting state, and only then call it.
 *
 * No success response should be transmitted if the
 * hardware operation cannot be completed successfully.
 *
 * On failure, response remains unchanged.
 *
 * This function never transmits on RS-485.
 */
bool dispatcher_prepare_set_outputs_response(const protocol_packet_t *request,
                                             uint8_t device_address,
                                             uint8_t accepted_mask,
                                             protocol_packet_t *response);

#endif
