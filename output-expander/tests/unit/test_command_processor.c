
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

  context.device_address = 0x23U;
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
 * All ordinary test packets represent data already
 * accepted by the protocol decoder.
 *
 * Individual tests modify command-specific fields
 * to exercise validation and error responses.
 */
static protocol_packet_t make_request(uint8_t command) {
  protocol_packet_t packet = {0};

  packet.destination = 0x23U;
  packet.sequence = 0x42U;
  packet.type = PROTOCOL_TYPE_REQUEST;
  packet.command = command;

  return packet;
}

/*
 * Verify a complete protocol error response.
 *
 * WHAT:
 * Check TYPE, destination, sequence, command, length
 * and the exact error-code payload.
 *
 * WHY:
 * Matching just the error code would not detect a
 * broken response header that causes the master
 * to reject an otherwise correctly encoded packet.
 *
 * Every error response must contain precisely one
 * payload byte.
 */
static void assert_error_response(const protocol_packet_t *request,
                                  const protocol_packet_t *response,
                                  uint8_t expected_error) {

  assert(response->destination == request->destination);
  assert(response->sequence == request->sequence);
  assert(response->command == request->command);

  assert(response->type == PROTOCOL_TYPE_ERROR);
  assert(response->payload_length == 1U);
  assert(response->payload[0] == expected_error);

  /*
   * The production error builder zero-initializes the
   * complete packet before filling its defined fields.
   *
   * Check unused payload storage as an additional
   * deterministic-output invariant.
   */
  for (size_t i = 1U; i < PROTOCOL_MAX_PAYLOAD; ++i) {
    assert(response->payload[i] == 0U);
  }
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

  assert(response.destination == 0x23U);
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
 *
 * Silent rejection remains different from returning
 * a protocol error response.
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

  /*
   * Broadcast ASSIGN_ADDRESS_BY_UID is recognized by the
   * dispatcher, but commissioning is not yet
   * implemented and must remain completely silent.
   */
  request.command = DISPATCHER_COMMAND_ASSIGN_ADDRESS_BY_UID;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Restore the correct destination and output command. */
  request.destination = 0x23U;
  request.command = DISPATCHER_COMMAND_SET_OUTPUTS;

  /* Responses must not be executed as commands. */
  request.type = PROTOCOL_TYPE_RESPONSE;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /* Error packets must also be ignored. */
  request.type = PROTOCOL_TYPE_ERROR;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  request.type = PROTOCOL_TYPE_REQUEST;

  /*
   * An uncommissioned device cannot execute
   * addressed SET_OUTPUTS commands or generate
   * an error response for such traffic.
   */
  context.device_address = 0xFFU;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(fake_output_mask == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);

  /*
   * Neither reserved address may act as an ordinary
   * commissioned device address.
   */
  context.device_address = 0x00U;

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Verify BAD_LENGTH for malformed read-only requests.
 *
 * WHAT:
 * Test both zero-length commands with an unexpected
 * payload byte.
 *
 * WHY:
 * A packet may have a valid CRC but still violate
 * its command-specific request format.
 *
 * These requests must receive an error response
 * without attempting any GPIO modification.
 */
static void test_invalid_read_lengths(void) {
  const command_processor_context_t context = make_context();

  const uint8_t commands[] = {DISPATCHER_COMMAND_PING,
                              DISPATCHER_COMMAND_GET_OUTPUTS};

  output_driver_init();

  for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); ++i) {

    protocol_packet_t request = make_request(commands[i]);
    request.payload_length = 1U;
    request.payload[0] = 0xA5U;

    protocol_packet_t response = {0};

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_RESPONSE_READY);

    assert_error_response(&request, &response, COMMAND_ERROR_BAD_LENGTH);

    assert(fake_set_calls == 0U);
    assert(fake_output_mask == 0U);
  }
}

/*
 * Verify malformed SET_OUTPUTS requests cannot
 * change logical or physical output state.
 *
 * WHAT:
 * Exercise both missing and excess payload bytes.
 *
 * WHY:
 * An invalid output command must return BAD_LENGTH
 * before output_driver_set() can be called.
 */
static void test_invalid_set_outputs_lengths(void) {
  const command_processor_context_t context = make_context();

  output_driver_init();
  fake_output_mask = 0x55U;

  const uint8_t invalid_lengths[] = {0U, 2U, 24U};

  for (size_t i = 0U; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]);
       ++i) {

    protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

    request.payload_length = invalid_lengths[i];
    request.payload[0] = 0xFFU;

    protocol_packet_t response = {0};

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_RESPONSE_READY);

    assert_error_response(&request, &response, COMMAND_ERROR_BAD_LENGTH);

    assert(fake_set_calls == 0U);
    assert(fake_output_mask == 0x55U);
  }
}

/*
 * Verify UNKNOWN_COMMAND for identifiers not
 * defined by the protocol.
 *
 * These are correctly addressed, validated
 * unicast requests.
 *
 * Unknown commands must never modify outputs.
 */
static void test_unknown_commands(void) {
  const command_processor_context_t context = make_context();

  const uint8_t unknown_commands[] = {0x00U, 0x03U, 0x12U, 0x31U, 0xEEU, 0xFFU};

  output_driver_init();

  for (size_t i = 0U;
       i < sizeof(unknown_commands) / sizeof(unknown_commands[0]); ++i) {

    protocol_packet_t request = make_request(unknown_commands[i]);

    protocol_packet_t response = {0};

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_RESPONSE_READY);

    assert_error_response(&request, &response, COMMAND_ERROR_UNKNOWN_COMMAND);

    assert(fake_set_calls == 0U);
    assert(fake_output_mask == 0U);
  }
}

/*
 * Verify that defined but unavailable commands
 * return UNSUPPORTED rather than UNKNOWN_COMMAND.
 *
 * WHAT:
 * Cover the remaining protocol v1 commands, including
 * unicast ASSIGN_ADDRESS_BY_UID, which is not an allowed form
 * of the broadcast commissioning request.
 *
 * WHY:
 * The master must be able to distinguish unsupported
 * functionality from undefined command identifiers.
 */
static void test_unsupported_commands(void) {
  const command_processor_context_t context = make_context();

  const uint8_t unsupported_commands[] = {
      0x02U, /* GET_STATUS */
      0x20U, /* GET_CONFIG */
      0x21U, /* SET_CONFIG */
      0x30U, /* ASSIGN_ADDRESS_BY_UID */
      0x40U  /* RESET */
  };

  output_driver_init();
  fake_output_mask = 0x81U;

  for (size_t i = 0U;
       i < sizeof(unsupported_commands) / sizeof(unsupported_commands[0]);
       ++i) {

    protocol_packet_t request = make_request(unsupported_commands[i]);

    protocol_packet_t response = {0};

    assert(command_processor_process(&context, &request, &response) ==
           COMMAND_PROCESSOR_RESPONSE_READY);

    assert_error_response(&request, &response, COMMAND_ERROR_UNSUPPORTED);

    assert(fake_set_calls == 0U);
    assert(fake_output_mask == 0x81U);
  }
}

/*
 * Verify that an unknown command sent to the wrong
 * device does not cause any error transmission.
 *
 * WHY:
 * Error replies are only permitted after successful
 * unicast address filtering.
 *
 * If multiple devices answered the same unrelated
 * request, they could collide on the shared RS-485 bus.
 */
static void test_unknown_command_wrong_address(void) {
  const command_processor_context_t context = make_context();

  protocol_packet_t request = make_request(0xEEU);
  request.destination = 0x18U;

  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  output_driver_init();

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);
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
 * Verify an impossible in-memory payload length is
 * rejected defensively without generating a reply.
 *
 * WHY:
 * protocol_decode() normally rejects such packets
 * before they reach this function.
 *
 * This test exercises the additional protection for
 * accidental misuse by a future internal caller.
 */
static void test_oversized_internal_length(void) {
  const command_processor_context_t context = make_context();

  protocol_packet_t request = make_request(DISPATCHER_COMMAND_SET_OUTPUTS);

  request.payload_length = (uint8_t)(PROTOCOL_MAX_PAYLOAD + 1U);

  protocol_packet_t response;
  memset(&response, 0xA5, sizeof(response));

  const protocol_packet_t original = response;

  output_driver_init();

  assert(command_processor_process(&context, &request, &response) ==
         COMMAND_PROCESSOR_IGNORED);

  assert(fake_set_calls == 0U);
  assert(memcmp(&response, &original, sizeof(response)) == 0);
}

/*
 * Execute the complete command-processor test suite.
 *
 * Each test uses assertions so CTest detects failures.
 * The host-debug build also enables ASan and UBSan.
 *
 * The tests verify both:
 *
 * - Positive behavior for permitted commands.
 * - Negative behavior preventing unsafe output changes
 *   or unexpected replies on a shared RS-485 bus.
 */
int main(void) {
  test_ping();
  test_get_outputs();
  test_set_outputs_all_masks();

  test_ignored_requests();
  test_invalid_read_lengths();
  test_invalid_set_outputs_lengths();

  test_unknown_commands();
  test_unsupported_commands();
  test_unknown_command_wrong_address();

  test_invalid_arguments();
  test_oversized_internal_length();

  return 0;
}
