
#include "command_processor.h"
#include "output_driver.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Simulated output driver.
 *
 * WHAT:
 * Track the logical output mask and number of writes.
 *
 * WHY:
 * The production command processor must be tested
 * without accessing actual CH32V003 GPIO registers.
 *
 * This mock checks that validation occurs before
 * the code attempts any physical output operation.
 */
static uint8_t fake_output_mask;
static unsigned int fake_set_calls;

void output_driver_init(void) {
  fake_output_mask = 0U;
  fake_set_calls = 0U;
}

uint8_t output_driver_get(void) { return fake_output_mask; }

uint8_t output_driver_set(uint8_t requested_mask) {
  fake_output_mask = requested_mask;
  ++fake_set_calls;
  return fake_output_mask;
}

/*
 * Create a known commissioned-device context.
 *
 * UID values are deliberately deterministic so
 * the resulting PING response can be verified.
 */
static command_processor_context_t make_context(void) {
  command_processor_context_t context = {0};

  context.device_address = 0x17U;
  context.firmware_major = 1U;
  context.firmware_minor = 2U;

  for (unsigned int i = 0U; i < DISPATCHER_UID_SIZE; ++i) {
    context.uid[i] = (uint8_t)i;
  }

  return context;
}

/*
 * Construct a decoded unicast request.
 *
 * All test packets represent data already accepted
 * by the protocol decoder. Individual tests modify
 * command-specific fields to exercise rejection.
 */
static protocol_packet_t make_request(uint8_t command) {
  protocol_packet_t packet = {0};

  packet.destination = 0x17U;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.command = command;

  return packet;
}

/*
 * Verify successful PING processing.
 *
 * PING must not change output state.
 * The response must contain the complete UID and
 * the two firmware version bytes.
 */
static void test_ping(void) {
  output_driver_init();

  const command_processor_context_t context = make_context();

  const protocol_packet_t request = make_request(DISPATCHER_COMMAND_PING);

  protocol_packet_t response = {0};

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_RESPONSE_READY);

  assert(response.destination == 0x17U);
  assert(response.sequence == 0x42U);
  assert(response.type == PROTOCOL_TYPE_RESPONSE);
  assert(response.command == DISPATCHER_COMMAND_PING);
  assert(response.payload_length == DISPATCHER_PING_RESPONSE_SIZE);

  assert(memcmp(response.payload, context.uid, DISPATCHER_UID_SIZE) == 0);

  assert(response.payload[12] == 1U);
  assert(response.payload[13] == 2U);

  assert(fake_set_calls == 0U);
  assert(fake_output_mask == 0U);
}

/*
 * Verify GET_OUTPUTS reports the current mask.
 *
 * Reading outputs must not cause any GPIO writes.
 */
static void test_get_outputs(void) {
  output_driver_init();
  fake_output_mask = 0x85U;

  const command_processor_context_t context = make_context();

  const protocol_packet_t request =
      make_request(DISPATCHER_COMMAND_GET_OUTPUTS);

  protocol_packet_t response = {0};

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_RESPONSE_READY);

  assert(response.destination == request.destination);
  assert(response.sequence == request.sequence);
  assert(response.type == PROTOCOL_TYPE_RESPONSE);
  assert(response.command == DISPATCHER_COMMAND_GET_OUTPUTS);
  assert(response.payload_length == 1U);
  assert(response.payload[0] == 0x85U);

  assert(fake_set_calls == 0U);
  assert(fake_output_mask == 0x85U);
}

/*
 * Verify SET_OUTPUTS applies the requested state
 * and acknowledges the exact applied logical mask.
 *
 * All 256 masks are tested because every eight-bit
 * output combination is valid.
 */
static void test_set_outputs_all_masks(void) {
  const command_processor_context_t context = make_context();

  for (unsigned int mask = 0U; mask < 256U; ++mask) {
    output_driver_init();

    protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

    request.payload_length = 1U;
    request.payload[0] = (uint8_t)mask;

    protocol_packet_t response = {0};

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_RESPONSE_READY);

    assert(fake_set_calls == 1U);
    assert(fake_output_mask == (uint8_t)mask);

    assert(response.destination == request.destination);
    assert(response.sequence == request.sequence);
    assert(response.type == PROTOCOL_TYPE_RESPONSE);
    assert(response.command == DISPATCHER_COMMAND_SET_OUTPUTS);
    assert(response.payload_length == 1U);
    assert(response.payload[0] == (uint8_t)mask);
  }
}

/*
 * Verify that rejected packets never change outputs
 * or overwrite the caller's response storage.
 *
 * This is essential for multidrop RS-485 operation:
 * a packet intended for another device must not
 * activate loads connected to this device.
 */
static void test_ignored_requests(void) {
  command_processor_context_t context = make_context();

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0xFFU;

  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  output_driver_init();

  /* Request addressed to another device. */
  request.destination = 0x18U;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Broadcast SET_OUTPUTS must be ignored. */
  request.destination = 0x00U;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Restore the correct destination. */
  request.destination = 0x17U;

  /* Responses must not be executed as commands. */
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);

  /* Error packets must also be ignored. */
  request.type = PROTOCOL_TYPE_ERROR;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);

  request.type = PROTOCOL_TYPE_REQUEST;

  /* A zero-length SET_OUTPUTS payload is invalid. */
  request.payload_length = 0U;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);

  /* A two-byte SET_OUTPUTS payload is also invalid. */
  request.payload_length = 2U;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);

  request.payload_length = 1U;

  /*
   * An uncommissioned device cannot execute
   * addressed SET_OUTPUTS commands.
   */
  context.device_address = 0xFFU;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(fake_output_mask == 0U);

  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Verify malformed read-only requests do not produce
 * a response or modify GPIO state.
 */
static void test_invalid_read_lengths(void) {
  const command_processor_context_t context = make_context();

  const uint8_t commands[] = {DISPATCHER_COMMAND_PING,
                              DISPATCHER_COMMAND_GET_OUTPUTS};

  output_driver_init();

  for (size_t i = 0U; i < sizeof(commands); ++i) {
    protocol_packet_t request = make_request(commands[i]);

    request.payload_length = 1U;

    protocol_packet_t response;
    memset(&response, 0xA5, sizeof(response));

    const protocol_packet_t original = response;

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_IGNORED);

    assert(memcmp(&response, &original, sizeof(response)) == 0);

    assert(fake_set_calls == 0U);
  }
}

/*
 * Verify defensive NULL and overlapping argument checks.
 *
 * No GPIO operation may occur when processing cannot
 * safely read the request or publish the response.
 */
static void test_invalid_arguments(void) {
  const command_processor_context_t context = make_context();

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = 1U;
  request.payload[0] = 0xFFU;

  protocol_packet_t response = {0};

  output_driver_init();

  assert(command_processor_process(NULL, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(command_processor_process(&context, NULL, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(command_processor_process(&context, &request, NULL) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(command_processor_process(&context, &request, &request) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
}

/*
 * Unknown commands must never execute as SET_OUTPUTS.
 *
 * The final protocol dispatcher will construct an
 * appropriate error response for valid addressed
 * unknown commands. This preliminary processor does
 * not yet implement protocol error frames.
 */
static void test_unknown_command(void) {
  const command_processor_context_t context = make_context();

  const protocol_packet_t request = make_request(0xEEU);

  protocol_packet_t response = {0};

  output_driver_init();

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
}

/*
 * Execute the complete command-processor test suite.
 *
 * Each test uses assertions so CTest detects failures.
 * The host-debug build also enables ASan and UBSan.
 */
int main(void) {
  test_ping();
  test_get_outputs();
  test_set_outputs_all_masks();
  test_ignored_requests();
  test_invalid_read_lengths();
  test_invalid_arguments();
  test_unknown_command();

  return 0;
}
