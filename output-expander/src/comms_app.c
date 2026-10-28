
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
   * Reject invalid arguments without changing the caller's state.
   *
   * Broadcast 0x00 cannot identify an individual slave. Address 0xFF
   * is allowed only as the internal uncommissioned state.
   */
  if (app == NULL || uid == NULL || boot_address == 0x00U) {
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
   * Publish the complete context together. No interrupt handler
   * accesses the structure, so no locking or atomic operation is needed.
   */
  app->command = initial;
}

uint8_t comms_app_address(const comms_app_t *app) {
  if (app == NULL) {
    return 0xFFU;
  }
  return app->command.device_address;
}

comms_app_result_t comms_app_process(comms_app_t *app,
                                     const protocol_packet_t *request) {
  if (app == NULL || request == NULL) {
    return COMMS_APP_IGNORED;
  }

  /*
   * Classify the request before executing commands or transmitting.
   *
   * Wrong-address packets and non-request packets remain completely
   * silent on the shared RS-485 bus.
   */
  const dispatcher_route_t route =
      dispatcher_route_request(request, app->command.device_address);

  if (route == DISPATCHER_BROADCAST_ASSIGN_UID) {
    uint8_t proposed_address = 0xFFU;

    /*
     * All twelve UID bytes, the 13-byte payload length and the
     * new address range are validated by the existing commissioning
     * module. A mismatch leaves the current address unchanged.
     */
    if (!commissioning_validate_assign_uid(request, app->command.uid,
                                           &proposed_address)) {
      return COMMS_APP_IGNORED;
    }

    /*
     * TEMPORARY DEVIATION FROM PROTOCOL v1:
     *
     * Normally the new address is activated only after a successful
     * EEPROM write and readback. EEPROM is not installed yet.
     *
     * Apply the address to RAM instead, solely for development.
     * It will revert to COMMS_BOOT_ADDRESS at the next MCU reset.
     *
     * The command remains silent because all broadcast transactions
     * forbid responses. The master confirms the assignment using
     * unicast PING at the newly selected address.
     */
    if (app->command.device_address != proposed_address) {
      app->command.device_address = proposed_address;
    }

    return COMMS_APP_ADDRESS_CHANGED;
  }

  if (route != DISPATCHER_UNICAST_REQUEST) {
    return COMMS_APP_IGNORED;
  }

  /*
   * Construct the response separately from the received request.
   *
   * The command processor validates command-specific lengths before
   * changing outputs. It also constructs defined protocol errors.
   *
   * The structure is zero-initialized to avoid exposing unused stack
   * bytes in any future response-building changes.
   */
  protocol_packet_t response = {0};

  if (command_processor_process(&app->command, request, &response) !=
      COMMAND_PROCESSOR_RESPONSE_READY) {
    return COMMS_APP_IGNORED;
  }

  /*
   * response_tx_send() performs final packet eligibility checks,
   * COBS and CRC encoding, and the blocking USART transmission.
   *
   * The USART driver must wait for TC before releasing RS-485 DE.
   * No other foreground code may transmit concurrently.
   *
   * Never retry automatically here: a master owns transaction retries
   * and repeated side-effect commands must retain their SEQ semantics.
   */
  if (!response_tx_send(&response)) {
    return COMMS_APP_TRANSMIT_FAILED;
  }

  return COMMS_APP_RESPONSE_SENT;
}
