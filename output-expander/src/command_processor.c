
#include "command_processor.h"

#include "output_driver.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Commands defined by protocol v1 but not yet implemented
 * by the foreground command processor.
 *
 * WHAT:
 * Identify valid protocol commands separately from unknown
 * command identifiers.
 *
 * WHY:
 * The protocol defines different error responses:
 *
 * UNKNOWN_COMMAND:
 *   The command identifier is not defined by protocol v1.
 *
 * UNSUPPORTED:
 *   The command is defined but currently unavailable on
 *   this firmware implementation.
 *
 * These constants remain private until the corresponding
 * command implementations are introduced.
 *
 * ASSIGN_ADDRESS_BY_UID is also defined by the existing dispatcher,
 * but it is a broadcast-only commissioning command.
 */
#define COMMAND_GET_STATUS 0x02U
#define COMMAND_GET_CONFIG 0x20U
#define COMMAND_SET_CONFIG 0x21U
#define COMMAND_FACTORY_RESET 0x22U
#define COMMAND_RESET 0x40U
#define OUTPUT_DELAY_PAYLOAD_SIZE 1U

/*
 * Build a protocol error response.
 *
 * WHAT:
 * Construct the complete decoded response packet using
 * the identity fields from the original request.
 *
 * WHY:
 * The master matches replies against the outstanding
 * destination, sequence number and command.
 *
 * Error responses use:
 *
 *   TYPE = PROTOCOL_TYPE_ERROR
 *   LEN = 1
 *   PAYLOAD[0] = error code
 *
 * The caller must already have verified that the request
 * is an addressed unicast request.
 *
 * This helper performs no transmission or GPIO operation.
 *
 * It builds into local storage before publishing the
 * response, so the caller never observes a partially
 * constructed packet.
 */
static command_processor_result_t
prepare_error_response(const protocol_packet_t *request, uint8_t error_code,
                       protocol_packet_t *response) {

  protocol_packet_t prepared = {0};

  prepared.destination = request->destination;
  prepared.sequence = request->sequence;
  prepared.type = PROTOCOL_TYPE_ERROR;
  prepared.command = request->command;
  prepared.payload_length = 1U;
  prepared.payload[0] = error_code;

  /*
   * Assign the completed structure only after every
   * response field has been initialized.
   *
   * C structure assignment copies the entire packet
   * object, including its fixed-capacity payload array.
   *
   * Zero-initialization above ensures unused payload
   * bytes do not contain uninitialized stack data.
   */
  *response = prepared;

  return COMMAND_PROCESSOR_RESPONSE_READY;
}

command_processor_result_t
command_processor_process(const command_processor_context_t *context,
                          const protocol_packet_t *request,
                          protocol_packet_t *response) {

  /*
   * Reject missing context or packet storage.
   *
   * Request and response must not be the same object,
   * because constructing a response overwrites fields
   * from the incoming request.
   *
   * No output change is permitted if any argument is
   * invalid.
   */
  if (context == NULL || request == NULL || response == NULL ||
      request == response) {
    return COMMAND_PROCESSOR_IGNORED;
  }

  /*
   * Filter packet type and destination before dispatch.
   *
   * WHAT:
   * Reuse the previously tested routing implementation.
   *
   * WHY:
   * All devices on the RS-485 multidrop bus receive
   * shared traffic. Only a matching commissioned
   * device may execute unicast commands.
   *
   * A wrong destination must be discarded silently,
   * not answered with an error packet.
   *
   * Broadcast commissioning is not implemented here.
   * In particular, broadcast packets must not produce
   * responses or activate outputs.
   */
  if (dispatcher_route_request(request, context->device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return COMMAND_PROCESSOR_IGNORED;
  }

  /*
   * Defensive packet-length validation.
   *
   * WHAT:
   * Reject an impossible declared payload length before
   * evaluating command-specific fields.
   *
   * WHY:
   * A normal caller supplies only packets accepted by
   * protocol_decode(), which already enforces this limit.
   *
   * Keeping this guard prevents a future caller from
   * accidentally treating an oversized packet object
   * as a valid command.
   *
   * This condition is not expected from real bus traffic
   * passing through the verified decoder.
   */
  if (request->payload_length > PROTOCOL_MAX_PAYLOAD) {
    return COMMAND_PROCESSOR_IGNORED;
  }

  /*
   * Execute commands in foreground context.
   *
   * The individual dispatcher functions perform
   * command-specific validation.
   *
   * Return a BAD_LENGTH error when a recognized command
   * has an invalid payload length.
   *
   * No invalid command may modify physical outputs.
   */
  switch (request->command) {

  case DISPATCHER_COMMAND_PING:
    /*
     * PING has no request payload.
     *
     * WHY:
     * Its response contains the physical UID and firmware
     * version; extra request bytes have no defined meaning.
     */
    if (request->payload_length != 0U) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_LENGTH,
                                    response);
    }

    /*
     * PING is read-only.
     *
     * The full physical UID and firmware version are
     * included in the success response.
     *
     * This stage constructs the response but does not
     * transmit it or refresh a hardware timer.
     */
    if (dispatcher_prepare_ping_response(request, context->device_address,
                                         context->uid, context->firmware_major,
                                         context->firmware_minor, response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;

  case DISPATCHER_COMMAND_GET_OUTPUTS:
    /*
     * GET_OUTPUTS also requires an empty request payload.
     *
     * The length check occurs before accessing the output
     * driver, so malformed requests have no GPIO effects.
     */
    if (request->payload_length != 0U) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_LENGTH,
                                    response);
    }

    /*
     * Read the current logical output mask.
     *
     * WHAT:
     * Ask the output driver for its last committed
     * GPIO output state.
     *
     * WHY:
     * GET_OUTPUTS must report the commanded output state,
     * not an earlier request or a physical active-low
     * GPIO register value.
     */
    if (dispatcher_prepare_get_outputs_response(
            request, context->device_address, output_driver_get(), response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;

  case DISPATCHER_COMMAND_GET_OUTPUT_DELAY: {
    if (request->payload_length != 0U) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_LENGTH,
                                    response);
    }

    /* One byte represents the active interval in exact 100 ms units. */
    const uint8_t delay_units = output_driver_get_activation_delay_units();
    protocol_packet_t prepared = {0};
    prepared.destination = request->destination;
    prepared.sequence = request->sequence;
    prepared.type = PROTOCOL_TYPE_RESPONSE;
    prepared.command = request->command;
    prepared.payload_length = OUTPUT_DELAY_PAYLOAD_SIZE;
    prepared.payload[0] = delay_units;
    *response = prepared;
    return COMMAND_PROCESSOR_RESPONSE_READY;
  }

  case DISPATCHER_COMMAND_SET_OUTPUT_DELAY: {
    if (request->payload_length != OUTPUT_DELAY_PAYLOAD_SIZE) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_LENGTH,
                                    response);
    }

    const uint8_t delay_units = request->payload[0];

    if (!output_driver_set_activation_delay_units(delay_units)) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_VALUE, response);
    }

    /* Echo only after the driver has validated and accepted the new value. */
    protocol_packet_t prepared = {0};
    prepared.destination = request->destination;
    prepared.sequence = request->sequence;
    prepared.type = PROTOCOL_TYPE_RESPONSE;
    prepared.command = request->command;
    prepared.payload_length = OUTPUT_DELAY_PAYLOAD_SIZE;
    prepared.payload[0] = request->payload[0];
    *response = prepared;
    return COMMAND_PROCESSOR_RESPONSE_READY;
  }

  case DISPATCHER_COMMAND_SET_OUTPUTS: {
    /*
     * SET_OUTPUTS requires exactly one byte.
     *
     * All 256 possible logical masks are permitted.
     * There are no reserved output bits.
     *
     * The payload-length check must precede the GPIO
     * operation to avoid partial or unintended changes.
     */
    if (request->payload_length != 1U) {
      return prepare_error_response(request, COMMAND_ERROR_BAD_LENGTH,
                                    response);
    }

    /*
     * Validate the complete SET_OUTPUTS request before
     * changing physical GPIO outputs.
     *
     * A valid COBS frame and CRC do not guarantee
     * command-specific validity.
     */
    uint8_t requested_mask = 0U;

    if (!dispatcher_parse_set_outputs(request, context->device_address,
                                      &requested_mask)) {
      return COMMAND_PROCESSOR_IGNORED;
    }

    /*
     * Replace the sequencer's complete target mask. Any required OFF changes
     * happen immediately, but ON changes remain pending for timed foreground
     * service. This volatile operation never writes EEPROM.
     */
    const uint8_t accepted_mask = output_driver_request(requested_mask);

    /*
     * Acknowledge the target accepted into RAM, not completion of the delayed
     * activation ramp. Waiting for all channels would exceed the normal bus
     * response deadline and invite retries. GET_OUTPUTS reports actual GPIO.
     */
    if (dispatcher_prepare_set_outputs_response(
            request, context->device_address, accepted_mask, response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;
  }

  case COMMAND_GET_STATUS:
  case COMMAND_GET_CONFIG:
  case COMMAND_SET_CONFIG:
  case COMMAND_FACTORY_RESET:
  case DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID:
  case COMMAND_RESET:
    /*
     * These commands are defined by protocol v1 but are
     * unavailable in the current foreground processor.
     *
     * GET_STATUS requires status/input integration.
     * GET_CONFIG and SET_CONFIG require persistent storage.
     * ASSIGN_ADDRESS_BY_UID requires UID matching and EEPROM writes.
     * RESET requires transmission-complete coordination.
     *
     * WHAT:
     * Return UNSUPPORTED rather than executing a partial
     * implementation or accepting the command silently.
     *
     * WHY:
     * The master can distinguish unavailable functionality
     * from an entirely unknown command identifier.
     *
     * ASSIGN_ADDRESS_BY_UID broadcast requests were already filtered
     * out above, so they still receive no response.
     */
    return prepare_error_response(request, COMMAND_ERROR_UNSUPPORTED, response);

  default:
    /*
     * A command not defined by protocol v1 receives
     * UNKNOWN_COMMAND.
     *
     * This is permitted only because earlier routing
     * confirmed that the packet is a valid addressed
     * unicast request.
     *
     * Unknown commands cannot change GPIO or EEPROM.
     */
    return prepare_error_response(request, COMMAND_ERROR_UNKNOWN_COMMAND,
                                  response);
  }
}
