
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
 * Simulated physical-output interface.
 *
 * The output driver normally calls the HAL to change active-low GPIO
 * pins. Here we track logical states so commands can be tested without
 * access to actual CH32V003 hardware.
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
 * Simulated RS-485 response transmitter.
 *
 * The production version encodes CRC-protected COBS frames, asserts DE,
 * waits for USART TC and releases the bus. Those layers already have
 * independent tests and are not duplicated in this mock.
 *
 * Capturing the packet checks the complete application decision path:
 * request routing, command execution and response eligibility.
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
 * Test UID with distinct bytes, including zero and values with bit 7 set.
 * This verifies exact matching rather than an integer reinterpretation.
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

/*
 * Check all three factory UID words are serialized in the declared order.
 */
static void test_uid_serialization(void) {
  uint8_t uid[DEVICE_UID_SIZE] = {0};

  device_uid_from_words(UINT32_C(0x56341200), UINT32_C(0xDEBC9A78),
                        UINT32_C(0x332211F0), uid);

  assert(memcmp(uid, test_uid, sizeof(test_uid)) == 0);
}

/*
 * Verify PING identity, version and request transaction fields.
 */
static void test_ping(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  const protocol_packet_t request =
      make_request(0x17U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(&app, &request) == COMMS_APP_RESPONSE_SENT);
  assert(response_count == 1U);
  assert(output_write_count == 0U);

  assert(last_response.destination == 0x17U);
  assert(last_response.sequence == 0x42U);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.command == DISPATCHER_COMMAND_PING);
  assert(last_response.payload_length == 14U);
  assert(memcmp(last_response.payload, test_uid, sizeof(test_uid)) == 0);
  assert(last_response.payload[12] == 1U);
  assert(last_response.payload[13] == 2U);
}

/*
 * Exercise SET_OUTPUTS followed by GET_OUTPUTS using real foreground
 * request routing and command processing.
 *
 * The logical mask is not inverted here: active-low GPIO conversion
 * remains the production output driver's responsibility.
 */
static void test_output_round_trip(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  protocol_packet_t set = make_request(0x17U, DISPATCHER_COMMAND_SET_OUTPUTS);

  set.payload_length = 1U;
  set.payload[0] = 0xA5U;

  assert(comms_app_process(&app, &set) == COMMS_APP_RESPONSE_SENT);
  assert(fake_outputs == 0xA5U);
  assert(output_write_count == 1U);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0xA5U);

  const protocol_packet_t get =
      make_request(0x17U, DISPATCHER_COMMAND_GET_OUTPUTS);

  assert(comms_app_process(&app, &get) == COMMS_APP_RESPONSE_SENT);
  assert(response_count == 2U);
  assert(output_write_count == 1U);
  assert(last_response.type == PROTOCOL_TYPE_RESPONSE);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0xA5U);
}

/*
 * Verify uncommissioned operation and broadcast RAM commissioning.
 *
 * Before assignment no ordinary unicast request can be executed.
 * After successful UID matching, the new address receives PING.
 * The broadcast itself produces no response.
 */
static void test_commissioning(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0xFFU);

  protocol_packet_t ping = make_request(0x17U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(&app, &ping) == COMMS_APP_IGNORED);
  assert(response_count == 0U);

  const protocol_packet_t assign = make_assign(0x23U);

  assert(comms_app_process(&app, &assign) == COMMS_APP_ADDRESS_CHANGED);

  assert(comms_app_address(&app) == 0x23U);
  assert(response_count == 0U);

  /* Previous address is no longer accepted. */
  assert(comms_app_process(&app, &ping) == COMMS_APP_IGNORED);
  assert(response_count == 0U);

  /* The newly assigned address now receives normal responses. */
  ping.destination = 0x23U;

  assert(comms_app_process(&app, &ping) == COMMS_APP_RESPONSE_SENT);
  assert(response_count == 1U);
  assert(last_response.destination == 0x23U);
  assert(memcmp(last_response.payload, test_uid, sizeof(test_uid)) == 0);

  /*
   * Restart the software context to simulate losing RAM on reset.
   * Without EEPROM, commissioning is intentionally nonpersistent.
   */
  init_app(&app, 0xFFU);
  assert(comms_app_address(&app) == 0xFFU);
}

/*
 * Reject UID mismatches, malformed payloads and reserved addresses.
 * No such packet may alter the current address or generate a response.
 */
static void test_invalid_commissioning(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  protocol_packet_t request = make_assign(0x23U);
  request.payload[5] ^= 1U;

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);
  assert(comms_app_address(&app) == 0x17U);

  request = make_assign(0x23U);
  request.payload_length = 12U;

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);
  assert(comms_app_address(&app) == 0x17U);

  request = make_assign(0x00U);

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);
  assert(comms_app_address(&app) == 0x17U);

  request = make_assign(0xFFU);

  assert(comms_app_process(&app, &request) == COMMS_APP_IGNORED);
  assert(comms_app_address(&app) == 0x17U);

  assert(response_count == 0U);
  assert(output_write_count == 0U);
}

/*
 * Wrong addresses, broadcast output commands and non-request packets
 * must remain silent and must not activate outputs.
 */
static void test_silent_rejections(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  protocol_packet_t packet =
      make_request(0x18U, DISPATCHER_COMMAND_SET_OUTPUTS);

  packet.payload_length = 1U;
  packet.payload[0] = 0xFFU;

  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.destination = 0x00U;
  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.destination = 0x17U;
  packet.type = PROTOCOL_TYPE_RESPONSE;
  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  packet.type = PROTOCOL_TYPE_ERROR;
  assert(comms_app_process(&app, &packet) == COMMS_APP_IGNORED);

  assert(response_count == 0U);
  assert(output_write_count == 0U);
  assert(fake_outputs == 0U);
}

/*
 * Verify that a valid, addressed but unknown command produces the
 * protocol's one-byte UNKNOWN_COMMAND error.
 *
 * This requires the completed Step 2.15.8 command processor.
 */
static void test_command_error(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  const protocol_packet_t packet = make_request(0x17U, 0xEEU);

  assert(comms_app_process(&app, &packet) == COMMS_APP_RESPONSE_SENT);
  assert(response_count == 1U);
  assert(last_response.type == PROTOCOL_TYPE_ERROR);
  assert(last_response.command == 0xEEU);
  assert(last_response.payload_length == 1U);
  assert(last_response.payload[0] == 0x01U);
  assert(output_write_count == 0U);
}

/*
 * A transport rejection must be observable by the foreground application.
 * Command execution is not silently rolled back: the transport reports
 * failure separately because the master controls retries.
 */
static void test_transport_failure(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  allow_transmission = false;

  const protocol_packet_t packet = make_request(0x17U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(&app, &packet) == COMMS_APP_TRANSMIT_FAILED);

  assert(response_count == 0U);
  assert(output_write_count == 0U);
}

static void test_invalid_arguments(void) {
  reset_fakes();

  comms_app_t app = {0};
  init_app(&app, 0x17U);

  const protocol_packet_t packet = make_request(0x17U, DISPATCHER_COMMAND_PING);

  assert(comms_app_process(NULL, &packet) == COMMS_APP_IGNORED);
  assert(comms_app_process(&app, NULL) == COMMS_APP_IGNORED);

  assert(response_count == 0U);
  assert(comms_app_address(NULL) == 0xFFU);
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
  test_invalid_arguments();

  return 0;
}
