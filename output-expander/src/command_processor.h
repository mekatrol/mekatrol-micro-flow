
#ifndef OUTPUT_EXPANDER_COMMAND_PROCESSOR_H
#define OUTPUT_EXPANDER_COMMAND_PROCESSOR_H

#include "dispatcher.h"
#include "protocol.h"

#include <stdint.h>

/*
 * Protocol v1 error codes.
 *
 * WHAT:
 * Identify why a well-formed, addressed request could
 * not be completed.
 *
 * WHY:
 * The master must distinguish unsupported functionality
 * from malformed command data and persistent-storage
 * failures.
 *
 * These values are defined by docs/protocol.md.
 *
 * The additional error codes are reserved here for
 * later implementation increments. They must not be
 * returned unless the corresponding condition has
 * actually occurred.
 */
#define COMMAND_ERROR_UNKNOWN_COMMAND 0x01U
#define COMMAND_ERROR_BAD_LENGTH 0x02U
#define COMMAND_ERROR_BAD_VALUE 0x03U
#define COMMAND_ERROR_STORAGE_ERROR 0x04U
#define COMMAND_ERROR_BUSY 0x05U
#define COMMAND_ERROR_UNSUPPORTED 0x06U

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
 * RESPONSE_READY includes both successful replies and
 * protocol error replies.
 *
 * The caller must inspect the response TYPE field when
 * it needs to distinguish success from an error.
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
 * Construct protocol error responses for invalid command
 * lengths, unknown commands and known commands that are
 * currently unavailable.
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
