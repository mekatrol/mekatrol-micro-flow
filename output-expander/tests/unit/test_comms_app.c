
#include "comms_app.h"
#include "device_uid.h"
#include "dispatcher.h"
#include "eeprom.h"
#include "output_driver.h"
#include "protocol.h"
#include "response_tx.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The communications executable links the production transactional store.
 * This byte array supplies its hardware boundary, allowing persistent-command
 * behavior to be exercised without memory-mapped I2C registers. */
static uint8_t fake_eeprom[256];

bool eeprom_read(uint16_t address, uint8_t *data, size_t length) {
  if ((data == NULL && length != 0U) || (size_t)address + length > sizeof(fake_eeprom)) {
    return false;
  }
  memcpy(data, &fake_eeprom[address], length);
  return true;
}

bool eeprom_write(uint16_t address, const uint8_t *data, size_t length) {
  if ((data == NULL && length != 0U) || (size_t)address + length > sizeof(fake_eeprom)) {
    return false;
  }
  memcpy(&fake_eeprom[address], data, length);
  return true;
}

/*
 * Simulated output driver.
 *
 * Host tests operate on logical masks and do not
 * access CH32V003 registers or drive physical loads.
 */
static uint8_t fake_outputs;
static uint8_t fake_delay_units;
static unsigned int output_write_count;

void output_driver_init(void) {
  fake_outputs = 0U;
  fake_delay_units = 5U;
  output_write_count = 0U;
}

uint8_t output_driver_get(void) { return fake_outputs; }

uint8_t output_driver_request(uint8_t requested_mask) {
  fake_outputs = requested_mask;
  ++output_write_count;
  return fake_outputs;
}

uint8_t output_driver_get_requested(void) { return fake_outputs; }
void output_driver_service(uint32_t now_ticks) { (void)now_ticks; }
uint8_t output_driver_get_activation_delay_units(void) {
  return fake_delay_units;
}
bool output_driver_set_activation_delay_units(uint8_t delay_units) {
  if (delay_units < OUTPUT_DRIVER_MIN_ACTIVATION_DELAY_UNITS ||
      delay_units > OUTPUT_DRIVER_MAX_ACTIVATION_DELAY_UNITS) {
    return false;
  }
  fake_delay_units = delay_units;
  return true;
}

/*
 * Simulated blocking RS-485 response transport.
 *
 * The production function completes UART transmission
 * and releases DE before returning true.
 *
 * This mock records the packet and simulates success
 * or rejection. It cannot independently verify the
 * hardware TC flag or electrical bus timing.
 */
static protocol_packet_t last_response;
static unsigned int response_count;
static bool allow_transmission;

bool response_tx_send(const protocol_packet_t *packet) {
  if (packet == NULL || !allow_transmission) {
    return false;
  }

  last_response = *packet;
  ++response_count;
  return true;
}

static void reset_fakes(void) {
  output_driver_init();
  memset(&last_response, 0, sizeof(last_response));
  response_count = 0U;
  allow_transmission = true;
}

/*
 * Deterministic synthetic UID with zero and high-bit
 * values to exercise exact byte comparison.
 */
static const uint8_t test_uid[DISPATCHER_UID_SIZE] = {
    0x00U, 0x12U, 0x34U, 0x56U, 0x78U, 0x9AU,
    0xBCU, 0xDEU, 0xF0U, 0x11U, 0x22U, 0x33U};

static void init_app(comms_app_t *app, uint8_t boot_address) {
  comms_app_init(app, boot_address, test_uid, 1U, 2U);
}

static protocol_packet_t make_request(uint8_t address, uint8_t command) {
  protocol_packet_t packet = {0};

  packet.destination = address;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.command = command;

  return packet;
}

static protocol_packet_t make_assign(uint8_t address) {
  protocol_packet_t request =
      make_request(0x00U, DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID);

  request.payload_length = 13U;

  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    request.payload[i] = test_uid[i];
  }

  request.payload[12] = address;

  return request;
}

static protocol_packet_t make_discovery(uint8_t command) {
  return make_request(PROTOCOL_BROADCAST_ADDRESS, command);
}

/*
 * Verify that discovery never answers directly from the receive path. The
 * foreground service must wait for the UID-seeded delay and then return the
 * complete identity and current address using the master's sequence number.
 */
static void test_discovery_delayed_response(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);
  const uint32_t started = UINT32_C(1234567);
  const protocol_packet_t discover =
      make_discovery(DISPATCHER_COMMAND_DISCOVER_DEVICES);

  assert(comms_app_process_at(&app, &discover, started) == COMMS_APP_IGNORED);
  assert(comms_app_discovery_pending(&app));
  assert(response_count == 0U);
  assert(comms_app_service(&app, started) == COMMS_APP_IGNORED);

  /* Every legal random delay is at most 100 ms, so 101 ms must be due. */
  assert(comms_app_service(&app, started + 101U * COMMS_TICKS_PER_MS) ==
         COMMS_APP_RESPONSE_SENT);
  assert(response_count == 1U);
  assert(!comms_app_discovery_pending(&app));
  assert(last_response.destination == 0x23U);
  assert(last_response.sequence == discover.sequence);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.command == DISPATCHER_COMMAND_DISCOVER_DEVICES);
  assert(last_response.payload_length == DISPATCHER_UID_SIZE + 1U);
  assert(memcmp(last_response.payload, test_uid, sizeof(test_uid)) == 0);
  assert(last_response.payload[DISPATCHER_UID_SIZE] == 0x23U);

  /* A device reports only once for a request that completed successfully. */
  assert(comms_app_service(&app, started + 200U * COMMS_TICKS_PER_MS) ==
         COMMS_APP_IGNORED);
  assert(response_count == 1U);
}

/*
 * A peer reply means the bus was occupied. The waiting device must begin a
 * new random quiet gap after that complete frame rather than transmitting on
 * the old deadline and risking overlap with the peer's final bytes.
 */
static void test_discovery_peer_defers_response(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);
  const protocol_packet_t discover =
      make_discovery(DISPATCHER_COMMAND_DISCOVER_DEVICES);
  assert(comms_app_process_at(&app, &discover, 0U) == COMMS_APP_IGNORED);

  protocol_packet_t peer = {0};
  peer.destination = 0x44U;
  peer.sequence = discover.sequence;
  peer.type = PROTOCOL_TYPE_RESPONSE;
  peer.command = DISPATCHER_COMMAND_DISCOVER_DEVICES;
  peer.payload_length = DISPATCHER_UID_SIZE + 1U;

  const uint32_t peer_finished = 100U * COMMS_TICKS_PER_MS;
  assert(comms_app_process_at(&app, &peer, peer_finished) == COMMS_APP_IGNORED);
  assert(comms_app_service(&app, peer_finished) == COMMS_APP_IGNORED);
  assert(response_count == 0U);

  assert(comms_app_service(&app, peer_finished + 101U * COMMS_TICKS_PER_MS) ==
         COMMS_APP_RESPONSE_SENT);
  assert(response_count == 1U);
}

/* Cancel and the hard deadline must both disarm a reply without transmitting. */
static void test_discovery_cancel_and_timeout(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);
  protocol_packet_t discover =
      make_discovery(DISPATCHER_COMMAND_DISCOVER_DEVICES);
  assert(comms_app_process_at(&app, &discover, 0U) == COMMS_APP_IGNORED);

  protocol_packet_t cancel =
      make_discovery(DISPATCHER_COMMAND_CANCEL_DISCOVERY);
  cancel.sequence = 0x99U; /* Cancel applies even if the master's SEQ changed. */
  assert(comms_app_process_at(&app, &cancel, COMMS_TICKS_PER_MS) ==
         COMMS_APP_IGNORED);
  assert(!comms_app_discovery_pending(&app));
  assert(comms_app_service(&app, 200U * COMMS_TICKS_PER_MS) ==
         COMMS_APP_IGNORED);
  assert(response_count == 0U);

  assert(comms_app_process_at(&app, &discover, 500U) == COMMS_APP_IGNORED);
  assert(comms_app_discovery_pending(&app));
  assert(comms_app_service(
             &app, 500U + COMMS_DISCOVERY_TIMEOUT_MS * COMMS_TICKS_PER_MS) ==
         COMMS_APP_IGNORED);
  assert(!comms_app_discovery_pending(&app));
  assert(response_count == 0U);
}

/* The current address byte also represents the reserved uncommissioned state. */
static void test_discovery_uncommissioned_address(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, PROTOCOL_UNCOMMISSIONED_ADDRESS);
  const protocol_packet_t discover =
      make_discovery(DISPATCHER_COMMAND_DISCOVER_DEVICES);

  assert(comms_app_process_at(&app, &discover, 0U) == COMMS_APP_IGNORED);
  assert(comms_app_service(&app, 101U * COMMS_TICKS_PER_MS) ==
         COMMS_APP_RESPONSE_SENT);
  assert(last_response.destination == PROTOCOL_UNCOMMISSIONED_ADDRESS);
  assert(last_response.payload[DISPATCHER_UID_SIZE] ==
         PROTOCOL_UNCOMMISSIONED_ADDRESS);
}

static void test_uid_serialization(void) {
  uint8_t uid[DEVICE_UID_SIZE] = {0};

  device_uid_from_words(UINT32_C(0x56341200), UINT32_C(0xDEBC9A78),
                        UINT32_C(0x332211F0), uid);

  assert(memcmp(uid, test_uid, sizeof(test_uid)) == 0);
}

/*
 * Verify PING identity and version are unaffected
 * by introducing watchdog-based reset support.
 */
static void test_ping(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  const protocol_packet_t request =
      make_request(0x23U, DISPATCHER_COMMAND_PING);

  assert(comms_app_watchdog_feed_allowed(&app));
  assert(comms_app_process(&app, &request) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 1U);
  assert(output_write_count == 0U);

  assert(last_response.destination == 0x23U);
  assert(last_response.sequence == 0x42U);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.command == DISPATCHER_COMMAND_PING);
  assert(last_response.payload_length == 14U);

  assert(memcmp(last_response.payload, test_uid, sizeof(test_uid)) == 0);

  assert(last_response.payload[12] == 1U);
  assert(last_response.payload[13] == 2U);

  assert(comms_app_watchdog_feed_allowed(&app));
}

/*
 * Verify SET_OUTPUTS and GET_OUTPUTS remain operational
 * with watchdog feeding enabled.
 */
static void test_output_round_trip(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  protocol_packet_t set = make_request(0x23U, DISPATCHER_COMMAND_SET_OUTPUTS);

  set.payload_length = 1U;
  set.payload[0] = 0xA5U;

  assert(comms_app_process(&app, &set) == COMMS_APP_RESPONSE_SENT);

  assert(fake_outputs == 0xA5U);
  assert(output_write_count == 1U);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0xA5U);

  const protocol_packet_t get =
      make_request(0x23U, DISPATCHER_COMMAND_GET_OUTPUTS);

  assert(comms_app_process(&app, &get) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 2U);
  assert(output_write_count == 1U);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0xA5U);

  assert(comms_app_watchdog_feed_allowed(&app));
}

/*
 * Verify normal UID commissioning behavior.
 *
 * It must not interfere with watchdog policy.
 */
static void test_commissioning(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0xFFU);

  protocol_packet_t ping = make_request(0x23U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(&app, &ping) == COMMS_APP_IGNORED);

  ping.destination = 0xFFU;
  assert(comms_app_process(&app, &ping) == COMMS_APP_RESPONSE_SENT);
  assert(last_response.destination == 0xFFU);
  assert(memcmp(last_response.payload, test_uid, sizeof(test_uid)) == 0);

  const protocol_packet_t assign = make_assign(0x23U);

  assert(comms_app_process(&app, &assign) == COMMS_APP_ADDRESS_CHANGED);

  assert(comms_app_address(&app) == 0x23U);
  assert(response_count == 1U);

  ping.destination = 0x23U;

  assert(comms_app_process(&app, &ping) == COMMS_APP_RESPONSE_SENT);

  assert(last_response.destination == 0x23U);
  assert(comms_app_watchdog_feed_allowed(&app));

  /*
   * Simulated restart restores the chosen boot address.
   * No EEPROM persistence is involved.
   */
  init_app(&app, 0xFFU);
  assert(comms_app_address(&app) == 0xFFU);
  assert(comms_app_watchdog_feed_allowed(&app));
}

static void test_invalid_commissioning(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  protocol_packet_t request = make_assign(0x23U);
  request.payload[5] ^= 1U;

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);

  request = make_assign(0x23U);
  request.payload_length = 12U;

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);

  request = make_assign(0x00U);

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);

  request = make_assign(0xFFU);

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);

  assert(comms_app_address(&app) == 0x23U);
  assert(response_count == 0U);
  assert(output_write_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

static void test_silent_rejections(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  protocol_packet_t packet =
      make_request(0x18U, DISPATCHER_COMMAND_SET_OUTPUTS);

  packet.payload_length = 1U;
  packet.payload[0] = 0xFFU;

  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.destination = 0x00U;

  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.destination = 0x23U;
  packet.type = PROTOCOL_TYPE_RESPONSE;

  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.type = PROTOCOL_TYPE_ERROR;

  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  assert(response_count == 0U);
  assert(output_write_count == 0U);
  assert(fake_outputs == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

static void test_command_error(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  const protocol_packet_t packet = make_request(0x23U, 0xEEU);

  assert(comms_app_process(&app, &packet) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 1U);
  assert(last_response.type == PROTOCOL_TYPE_ERROR);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0x01U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

static void test_transport_failure(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  allow_transmission = false;

  const protocol_packet_t packet = make_request(0x23U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(&app, &packet) == COMMS_APP_TRANSMIT_FAILED);

  assert(response_count == 0U);
  assert(output_write_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

/*
 * Verify successful RESET acknowledgment and latching.
 *
 * The mock returns true only after the simulated blocking
 * transmission completes, matching the contract of the
 * production response_tx_send().
 */
static void test_reset_success(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  const protocol_packet_t reset = make_request(0x23U, COMMS_COMMAND_RESET);

  assert(comms_app_watchdog_feed_allowed(&app));

  assert(comms_app_process(&app, &reset) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 1U);

  /*
   * Verify the entire protocol RESET response identity.
   * The successful response must have an empty payload.
   */
  assert(last_response.destination == reset.destination);
  assert(last_response.sequence == reset.sequence);
  assert(last_response.command == COMMS_COMMAND_RESET);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.payload_length == 0U);

  /*
   * Watchdog feeding stops only after acknowledgment.
   */
  assert(!comms_app_watchdog_feed_allowed(&app));

  /*
   * Once reset is pending, further requests must not
   * transmit or change physical outputs.
   */
  protocol_packet_t set = make_request(0x23U, DISPATCHER_COMMAND_SET_OUTPUTS);

  set.payload_length = 1U;
  set.payload[0] = 0xFFU;

  assert(comms_app_process(&app, &set) == COMMS_APP_IGNORED);

  assert(output_write_count == 0U);
  assert(fake_outputs == 0U);
  assert(response_count == 1U);
  assert(!comms_app_watchdog_feed_allowed(&app));
}

/*
 * An invalid RESET length must produce BAD_LENGTH
 * without disabling watchdog feeding.
 */
static void test_reset_invalid_length(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  protocol_packet_t reset = make_request(0x23U, COMMS_COMMAND_RESET);

  reset.payload_length = 1U;
  reset.payload[0] = 0xA5U;

  assert(comms_app_process(&app, &reset) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 1U);
  assert(last_response.type == PROTOCOL_TYPE_ERROR);
  assert(last_response.command == COMMS_COMMAND_RESET);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0x02U);

  assert(comms_app_watchdog_feed_allowed(&app));
  assert(output_write_count == 0U);
}

/*
 * RESET must not execute when it is broadcast,
 * addressed elsewhere, or received as a response.
 */
static void test_reset_invalid_routing(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  protocol_packet_t reset = make_request(0x18U, COMMS_COMMAND_RESET);

  assert(comms_app_process(&app, &reset) == COMMS_APP_IGNORED);

  reset.destination = 0x00U;

  assert(comms_app_process(&app, &reset) == COMMS_APP_IGNORED);

  reset.destination = 0x23U;
  reset.type = PROTOCOL_TYPE_RESPONSE;

  assert(comms_app_process(&app, &reset) == COMMS_APP_IGNORED);

  reset.type = PROTOCOL_TYPE_ERROR;

  assert(comms_app_process(&app, &reset) == COMMS_APP_IGNORED);

  assert(response_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

/*
 * Failed acknowledgment must not initiate reset.
 *
 * If transport rejects the response, the application
 * must keep feeding IWDG and remain available for a
 * master-controlled retry.
 */
static void test_reset_transmit_failure(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  allow_transmission = false;

  const protocol_packet_t reset = make_request(0x23U, COMMS_COMMAND_RESET);

  assert(comms_app_process(&app, &reset) == COMMS_APP_TRANSMIT_FAILED);

  assert(response_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));

  /*
   * A retry may succeed after the transport becomes
   * available again.
   */
  allow_transmission = true;

  assert(comms_app_process(&app, &reset) == COMMS_APP_RESPONSE_SENT);

  assert(response_count == 1U);
  assert(!comms_app_watchdog_feed_allowed(&app));
}

/*
 * Reinitialize application state to simulate startup
 * after a watchdog reset.
 *
 * The hardware watchdog restarts separately from
 * watchdog_start() in the real firmware.
 */
static void test_reset_reinitialization(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  const protocol_packet_t reset = make_request(0x23U, COMMS_COMMAND_RESET);

  assert(comms_app_process(&app, &reset) == COMMS_APP_RESPONSE_SENT);

  assert(!comms_app_watchdog_feed_allowed(&app));

  init_app(&app, 0x23U);

  assert(comms_app_watchdog_feed_allowed(&app));
  assert(comms_app_address(&app) == 0x23U);
}

/*
 * Invalid pointers must not cause processing or
 * accidentally authorize watchdog feeding.
 */
static void test_invalid_arguments(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x23U);

  const protocol_packet_t packet = make_request(0x23U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(NULL, &packet) == COMMS_APP_IGNORED);

  assert(comms_app_process(&app, NULL) == COMMS_APP_IGNORED);

  assert(comms_app_address(NULL) == 0xFFU);
  assert(!comms_app_watchdog_feed_allowed(NULL));
  assert(!comms_app_discovery_pending(NULL));

  assert(response_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

int main(void) {
  test_uid_serialization();
  test_discovery_delayed_response();
  test_discovery_peer_defers_response();
  test_discovery_cancel_and_timeout();
  test_discovery_uncommissioned_address();
  test_ping();
  test_output_round_trip();
  test_commissioning();
  test_invalid_commissioning();
  test_silent_rejections();
  test_command_error();
  test_transport_failure();

  test_reset_success();
  test_reset_invalid_length();
  test_reset_invalid_routing();
  test_reset_transmit_failure();
  test_reset_reinitialization();

  test_invalid_arguments();

  return 0;
}
