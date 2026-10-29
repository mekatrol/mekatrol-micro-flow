
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
  app->discovery_pending = false;
  app->discovery_sequence = 0U;
  app->discovery_started_ticks = 0U;
  app->discovery_reply_due_ticks = 0U;

  /*
   * Fold all twelve UID bytes into a non-zero PRNG state. The UID is stable
   * for a physical device and differs between devices, which gives otherwise
   * identical expanders different delay streams without needing entropy
   * hardware. This is collision avoidance, not cryptography.
   */
  app->discovery_random_state = UINT32_C(0x6D2B79F5);
  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    app->discovery_random_state ^= (uint32_t)uid[i] << ((i & 3U) * 8U);
    app->discovery_random_state =
        app->discovery_random_state * UINT32_C(1664525) + UINT32_C(1013904223);
  }
  if (app->discovery_random_state == 0U) {
    /* Xorshift's all-zero state cannot advance, so substitute a fixed non-zero
     * value for the one theoretical UID fold that could produce it. */
    app->discovery_random_state = UINT32_C(0xA341316C);
  }
}

static uint32_t discovery_random_delay_ticks(comms_app_t *app) {
  /*
   * Xorshift32 is tiny, deterministic and sufficient for distributing bus
   * replies. Advance the state for every postponement so two devices that
   * happened to choose the same first slot are unlikely to remain locked
   * together after hearing another response.
   */
  uint32_t value = app->discovery_random_state;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  app->discovery_random_state = value;

  const uint32_t delay_ms = COMMS_DISCOVERY_MIN_DELAY_MS +
                            (value % COMMS_DISCOVERY_DELAY_SPAN_MS);
  return delay_ms * COMMS_TICKS_PER_MS;
}

static bool discovery_has_expired(const comms_app_t *app,
                                  uint32_t now_ticks) {
  return (uint32_t)(now_ticks - app->discovery_started_ticks) >=
         COMMS_DISCOVERY_TIMEOUT_MS * COMMS_TICKS_PER_MS;
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

bool comms_app_discovery_pending(const comms_app_t *app) {
  /* A missing application cannot own a valid pending discovery operation. */
  return app != NULL && app->discovery_pending;
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

comms_app_result_t comms_app_process_at(comms_app_t *app,
                                        const protocol_packet_t *request,
                                        uint32_t now_ticks) {
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

  if (route == DISPATCHER_BROADCAST_DISCOVER_DEVICES) {
    /*
     * Only an empty request is meaningful. A malformed broadcast remains
     * silent because several devices returning an error would recreate the
     * collision that discovery is designed to avoid.
     *
     * A new valid request replaces an older scan. Record one common absolute
     * expiry point, then choose this device's first UID-seeded response gap.
     */
    if (request->payload_length != 0U) {
      return COMMS_APP_IGNORED;
    }

    app->discovery_pending = true;
    app->discovery_sequence = request->sequence;
    app->discovery_started_ticks = now_ticks;
    app->discovery_reply_due_ticks =
        now_ticks + discovery_random_delay_ticks(app);
    return COMMS_APP_IGNORED;
  }

  if (route == DISPATCHER_BROADCAST_CANCEL_DISCOVERY) {
    /*
     * Cancel is intentionally independent of sequence number. It means
     * "stop the outstanding bus scan" and therefore remains useful if the
     * master's sequence state changed after a restart. Invalid payloads do
     * not cancel anything, avoiding accidental cancellation by malformed
     * traffic.
     */
    if (request->payload_length == 0U) {
      app->discovery_pending = false;
    }
    return COMMS_APP_IGNORED;
  }

  /*
   * Every device receives discovery responses sent by its peers. If this
   * device is still waiting in the same scan, start a fresh random quiet gap
   * after the peer's complete frame. frame_receiver publishes packets only
   * after the delimiter and CRC have arrived, so now_ticks is already the
   * end of that transmission. The original fifteen-second deadline is never
   * extended, which guarantees eventual abandonment on a continuously busy
   * or pathological bus.
   */
  if (app->discovery_pending && request->type == PROTOCOL_TYPE_RESPONSE &&
      request->command == DISPATCHER_COMMAND_DISCOVER_DEVICES &&
      request->sequence == app->discovery_sequence) {
    if (discovery_has_expired(app, now_ticks)) {
      app->discovery_pending = false;
    } else {
      app->discovery_reply_due_ticks =
          now_ticks + discovery_random_delay_ticks(app);
    }
    return COMMS_APP_IGNORED;
  }

  if (route == DISPATCHER_BROADCAST_ASSIGN_ADDRESS_BY_UID) {
    uint8_t proposed_address = PROTOCOL_UNCOMMISSIONED_ADDRESS;

    /*
     * Accept only complete UID matches with a valid
     * commissioned address.
     *
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

comms_app_result_t comms_app_process(comms_app_t *app,
                                     const protocol_packet_t *request) {
  /*
   * Preserve the original API for host code that has no clock. Production
   * firmware uses comms_app_process_at(); ordinary immediate commands behave
   * identically when the synthetic timestamp is zero.
   */
  return comms_app_process_at(app, request, 0U);
}

comms_app_result_t comms_app_service(comms_app_t *app, uint32_t now_ticks) {
  if (app == NULL || !app->discovery_pending || app->reset_requested) {
    return COMMS_APP_IGNORED;
  }

  if (discovery_has_expired(app, now_ticks)) {
    app->discovery_pending = false;
    return COMMS_APP_IGNORED;
  }

  /* Signed subtraction is the conventional wrap-safe "deadline reached"
   * test. All discovery intervals are far below half the uint32_t tick range. */
  if ((int32_t)(now_ticks - app->discovery_reply_due_ticks) < 0) {
    return COMMS_APP_IGNORED;
  }

  protocol_packet_t response = {0};
  response.destination = app->command.device_address;
  response.sequence = app->discovery_sequence;
  response.type = PROTOCOL_TYPE_RESPONSE;
  response.command = DISPATCHER_COMMAND_DISCOVER_DEVICES;
  response.payload_length = DISPATCHER_UID_SIZE + 1U;

  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    response.payload[i] = app->command.uid[i];
  }
  response.payload[DISPATCHER_UID_SIZE] = app->command.device_address;

  /*
   * A successful send completes this device's participation. If the local
   * transmitter rejects the packet, retain the request and choose another
   * short delay so a transient failure can recover without a new master
   * request, while the original timeout still provides the hard stop.
   */
  if (!response_tx_send(&response)) {
    app->discovery_reply_due_ticks =
        now_ticks + discovery_random_delay_ticks(app);
    return COMMS_APP_TRANSMIT_FAILED;
  }

  app->discovery_pending = false;
  return COMMS_APP_RESPONSE_SENT;
}
