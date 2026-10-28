
#include "comms_app.h"

#include "commissioning.h"
#include "dispatcher.h"
#include "response_tx.h"

#include <stddef.h>
#include <stdint.h>

void comms_app_init(comms_app_t *app, uint8_t boot_address,
                    const uint8_t uid[DISPATCHER_UID_SIZE],
                    uint8_t firmware_major, uint8_t firmware_minor) {
  /*
   * Reject invalid initialization without modifying
   * application state.
   *
   * A zero address is reserved for broadcast.
   */
  if (app == NULL || uid == NULL ||
      boot_address == PROTOCOL_BROADCAST_ADDRESS) {
    return;
  }

  command_processor_context_t initial = {0};

  initial.device_address = boot_address;
  initial.firmware_major = firmware_major;
  initial.firmware_minor = firmware_minor;

  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    initial.uid[i] = uid[i];
  }

  /*
   * Only foreground code owns these fields.
   *
   * Clear reset_requested on initialization so normal
   * watchdog feeding resumes following hardware reset.
   */
  app->command = initial;
  app->reset_requested = false;
}

uint8_t comms_app_address(const comms_app_t *app) {
  if (app == NULL) {
    return PROTOCOL_UNCOMMISSIONED_ADDRESS;
  }

  return app->command.device_address;
}

bool comms_app_watchdog_feed_allowed(const comms_app_t *app) {
  /*
   * A missing application is not healthy foreground
   * state and must not authorize watchdog feeding.
   *
   * Once reset_requested is true, normal processing
   * cannot re-enable watchdog refreshes.
   */
  return app != NULL && !app->reset_requested;
}

/*
 * Build and acknowledge a valid RESET command.
 *
 * WHAT:
 * Construct an empty successful response and transmit
 * it through the existing blocking RS-485 transmitter.
 *
 * WHY:
 * Protocol v1 requires a valid response before reset.
 *
 * response_tx_send() encodes CRC/COBS and calls the
 * USART driver, which waits for TC and releases DE.
 *
 * The reset flag must be set only after that function
 * reports successful completion.
 */
static comms_app_result_t
process_reset_request(comms_app_t *app, const protocol_packet_t *request) {
  /*
   * RESET accepts no request payload.
   *
   * Invalid length receives BAD_LENGTH, rather than
   * silently resetting or returning a success response.
   *
   * A failed transmission leaves watchdog feeding
   * enabled because no valid acknowledgment completed.
   */
  protocol_packet_t response = {0};

  response.destination = request->destination;
  response.sequence = request->sequence;
  response.command = request->command;

  if (request->payload_length != 0U) {
    response.type = PROTOCOL_TYPE_ERROR;
    response.payload_length = 1U;
    response.payload[0] = 0x02U; /* BAD_LENGTH */

    if (!response_tx_send(&response)) {
      return COMMS_APP_TRANSMIT_FAILED;
    }

    return COMMS_APP_RESPONSE_SENT;
  }

  /*
   * Successful RESET response has no payload.
   *
   * The packet's TYPE is RESPONSE, not ERROR.
   * No additional fields or response bytes are needed.
   */
  response.type = PROTOCOL_TYPE_RESPONSE;
  response.payload_length = 0U;

  if (!response_tx_send(&response)) {
    return COMMS_APP_TRANSMIT_FAILED;
  }

  /*
   * Critical ordering requirement:
   *
   * The blocking sender has now finished USART TC
   * handling and released RS-485 DE.
   *
   * Only now may the foreground stop refreshing IWDG.
   * The MCU will reset when the watchdog expires.
   */
  app->reset_requested = true;

  return COMMS_APP_RESPONSE_SENT;
}

comms_app_result_t comms_app_process(comms_app_t *app,
                                     const protocol_packet_t *request) {
  if (app == NULL || request == NULL) {
    return COMMS_APP_IGNORED;
  }

  /*
   * Never execute another command after a successful
   * RESET acknowledgment.
   *
   * This prevents fresh SET_OUTPUTS requests from
   * changing loads while waiting for watchdog expiry.
   *
   * The foreground main loop also stops processing
   * additional queued frames after reset_requested.
   */
  if (app->reset_requested) {
    return COMMS_APP_IGNORED;
  }

  /*
   * Protect against malformed in-memory packet objects.
   *
   * Real wire packets already pass protocol_decode(),
   * which enforces the 24-byte maximum before dispatch.
   *
   * No oversized packet may cause a command execution.
   */
  if (request->payload_length > PROTOCOL_MAX_PAYLOAD) {
    return COMMS_APP_IGNORED;
  }

  /*
   * Route the packet before any command execution.
   *
   * Non-request packets and traffic for another device
   * must remain silent.
   */
  const dispatcher_route_t route =
      dispatcher_route_request(request, app->command.device_address);

  if (route == DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID) {
    uint8_t proposed_address = PROTOCOL_UNCOMMISSIONED_ADDRESS;

    /*
     * Accept only complete UID matches with a valid
     * commissioned address.
     *
     * This remains RAM-only until EEPROM is installed.
     * Broadcast requests do not produce responses.
     */
    if (!commissioning_validate_assign_address_by_uid(
            request, app->command.uid, &proposed_address)) {
      return COMMS_APP_IGNORED;
    }

    if (app->command.device_address != proposed_address) {
      app->command.device_address = proposed_address;
    }

    return COMMS_APP_ADDRESS_CHANGED;
  }

  if (route != DISPATCHER_UNICAST_REQUEST) {
    return COMMS_APP_IGNORED;
  }

  /*
   * RESET is handled at the application layer because
   * it changes foreground watchdog policy.
   *
   * The command processor remains responsible for all
   * previously implemented normal commands.
   *
   * The established command processor currently marks
   * RESET unsupported; intercepting it here ensures
   * precisely one successful RESET execution pathway.
   */
  if (request->command == COMMS_COMMAND_RESET) {
    return process_reset_request(app, request);
  }

  /*
   * Preserve existing PING, GET_OUTPUTS, SET_OUTPUTS
   * and protocol-error behavior without modification.
   */
  protocol_packet_t response = {0};

  if (command_processor_process(&app->command, request, &response) !=
      COMMAND_PROCESSOR_RESPONSE_READY) {
    return COMMS_APP_IGNORED;
  }

  /*
   * The blocking sender controls RS-485 direction,
   * frame encoding and USART transmission completion.
   */
  if (!response_tx_send(&response)) {
    return COMMS_APP_TRANSMIT_FAILED;
  }

  return COMMS_APP_RESPONSE_SENT;
}
