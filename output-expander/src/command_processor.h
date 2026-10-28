
#ifndef OUTPUT_EXPANDER_COMMAND_PROCESSOR_H
#define OUTPUT_EXPANDER_COMMAND_PROCESSOR_H

#include "dispatcher.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Foreground command execution context.
 *
 * WHAT:
 * Supply the device address, physical UID and firmware
 * version required when processing protocol requests.
 *
 * WHY:
 * These values originate from different hardware and
 * configuration sources:
 *
 * - Device address: validated EEPROM configuration.
 * - UID: CH32V003 factory-programmed unique identifier.
 * - Firmware version: application build configuration.
 *
 * Keeping them explicit avoids hardcoded addresses and
 * direct hardware register accesses in protocol logic.
 *
 * The caller owns this object and its lifetime.
 */
typedef struct {
  uint8_t device_address;
  uint8_t uid[DISPATCHER_UID_SIZE];
  uint8_t firmware_major;
  uint8_t firmware_minor;
} command_processor_context_t;

/*
 * Result of processing one already decoded packet.
 *
 * WHAT:
 * Distinguish ignored traffic from a successfully
 * constructed response.
 *
 * WHY:
 * Not every packet received from an RS-485 bus warrants
 * transmission. Incorrect addresses and broadcasts must
 * remain completely silent.
 *
 * Response transmission is intentionally excluded from
 * this module.
 */
typedef enum {
  COMMAND_PROCESSOR_IGNORED = 0,
  COMMAND_PROCESSOR_RESPONSE_READY
} command_processor_result_t;

/*
 * Process one validated packet in foreground context.
 *
 * WHAT:
 * Route the request and execute PING, GET_OUTPUTS or
 * SET_OUTPUTS using the existing dispatcher and output
 * driver implementations.
 *
 * WHY:
 * Protocol decoding and command execution must remain
 * outside the USART interrupt handler.
 *
 * This function may change GPIO through output_driver_set()
 * when a valid addressed SET_OUTPUTS request is received.
 *
 * It never transmits RS-485 data or writes EEPROM.
 *
 * On COMMAND_PROCESSOR_RESPONSE_READY, *response contains
 * a complete protocol packet ready for protocol_encode().
 *
 * On COMMAND_PROCESSOR_IGNORED, *response is unchanged.
 *
 * Request and response must be separate packet objects.
 *
 * The caller must not invoke this function from an ISR.
 */
command_processor_result_t
command_processor_process(const command_processor_context_t *context,
                          const protocol_packet_t *request,
                          protocol_packet_t *response);

#endif
