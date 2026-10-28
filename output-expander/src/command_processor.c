
#include "command_processor.h"

#include "output_driver.h"

#include <stddef.h>
#include <stdint.h>

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
   * Broadcast commissioning is not implemented here.
   * In particular, broadcast packets must not produce
   * responses or activate outputs.
   */
  if (dispatcher_route_request(request, context->device_address) !=
      DISPATCHER_UNICAST_REQUEST) {
    return COMMAND_PROCESSOR_IGNORED;
  }

  /*
   * Execute commands in foreground context.
   *
   * The individual dispatcher functions perform
   * command-specific payload validation.
   *
   * Invalid commands or lengths are not permitted
   * to modify physical output state.
   */
  switch (request->command) {

  case DISPATCHER_COMMAND_PING:
    /*
     * PING is read-only.
     *
     * The full physical UID and firmware version
     * are included in the success response.
     *
     * This stage constructs the response but does
     * not transmit it.
     */
    if (dispatcher_prepare_ping_response(request, context->device_address,
                                         context->uid, context->firmware_major,
                                         context->firmware_minor, response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;

  case DISPATCHER_COMMAND_GET_OUTPUTS:
    /*
     * Read the current logical output mask.
     *
     * WHAT:
     * Ask the output driver for its last committed
     * GPIO output state.
     *
     * WHY:
     * GET_OUTPUTS must report the actual commanded
     * output state rather than an earlier request or
     * a physical active-low GPIO register value.
     *
     * Validate the request before calling the driver.
     */
    if (request->payload_length != 0U) {
      return COMMAND_PROCESSOR_IGNORED;
    }

    if (dispatcher_prepare_get_outputs_response(
            request, context->device_address, output_driver_get(), response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;

  case DISPATCHER_COMMAND_SET_OUTPUTS: {
    /*
     * Validate the complete SET_OUTPUTS request
     * before changing physical GPIO outputs.
     *
     * A valid COBS frame and CRC do not guarantee
     * that the command has the correct payload length.
     */
    uint8_t requested_mask = 0U;

    if (!dispatcher_parse_set_outputs(request, context->device_address,
                                      &requested_mask)) {
      return COMMAND_PROCESSOR_IGNORED;
    }

    /*
     * Apply the requested logical mask.
     *
     * output_driver_set() calculates the necessary
     * transitions and calls the existing HAL.
     *
     * The HAL handles physical GPIO pin mapping and
     * active-low conversion.
     *
     * This operation changes volatile output state
     * only. No EEPROM write takes place.
     */
    const uint8_t applied_mask = output_driver_set(requested_mask);

    /*
     * Construct acknowledgment only after the
     * GPIO writes have been issued.
     *
     * The response contains the applied logical
     * mask, not an electrical GPIO register value.
     *
     * The current HAL provides no external load
     * feedback, so this confirms commanded GPIO
     * state rather than physical relay operation.
     */
    if (dispatcher_prepare_set_outputs_response(
            request, context->device_address, applied_mask, response)) {
      return COMMAND_PROCESSOR_RESPONSE_READY;
    }

    return COMMAND_PROCESSOR_IGNORED;
  }

  default:
    /*
     * Unsupported commands are not executed.
     *
     * Protocol v1 requires defined error responses
     * for valid addressed unicast requests.
     *
     * Error construction is not implemented yet,
     * so do not connect this processor to main.c
     * until that behavior is added and tested.
     */
    return COMMAND_PROCESSOR_IGNORED;
  }
}
