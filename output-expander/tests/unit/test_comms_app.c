
#include "comms_app.h"
#include "device_uid.h"
#include "dispatcher.h"
#include "output_driver.h"
#include "protocol.h"
#include "response_tx.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Simulated output driver.
 *
 * Host tests operate on logical masks and do not
 * access CH32V003 registers or drive physical loads.
 */
static uint8_t fake_outputs;
static unsigned int output_write_count;

void output_driver_init(void) {
  fake_outputs = 0U;
  output_write_count = 0U;
}

uint8_t output_driver_get(void) { return fake_outputs; }

uint8_t output_driver_set(uint8_t requested_mask) {
  fake_outputs = requested_mask;
  ++output_write_count;
  return fake_outputs;
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
      make_request(0x00U, DISPATCHER_COMMAND_ASSIGN_UID);

  request.payload_length = 13U;

  for (size_t i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    request.payload[i] = test_uid[i];
  }

  request.payload[12] = address;

  return request;
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
 * Verify normal RAM-only UID commissioning.
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

  assert(response_count == 0U);
  assert(comms_app_watchdog_feed_allowed(&app));
}

int main(void) {
  test_uid_serialization();
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
