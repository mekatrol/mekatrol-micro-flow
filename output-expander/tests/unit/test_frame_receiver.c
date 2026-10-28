
#include "frame_receiver.h"

#include "irq_rx.h"
#include "protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static frame_receiver_t receiver;

static const protocol_packet_t request = {.destination = 0x17U,
                                          .sequence = 0x42U,
                                          .type = PROTOCOL_TYPE_REQUEST,
                                          .command = 0x11U,
                                          .payload_length = 3U,
                                          .payload = {0U, 0x55U, 0x00U}};

static void reset(void) {
  irq_rx_init();
  frame_receiver_init(&receiver);
  assert(frame_receiver_fault_count(&receiver) == 0U);
  assert(!frame_receiver_fault_active(&receiver));
}

static void push(uint8_t type, uint8_t value) {
  const irq_rx_event_t event = {.type = type, .value = value};
  assert(irq_rx_push_from_isr(event));
}

static void byte(uint8_t value) { push(IRQ_RX_EVENT_BYTE, value); }

static size_t encoded(uint8_t *wire) {
  size_t length = 0U;

  assert(protocol_encode(&request, wire, PROTOCOL_MAX_ENCODED_SIZE, &length) ==
         PROTOCOL_OK);
  return length;
}

/*
 * Poll after each byte to simulate foreground consumption.
 *
 * WHY:
 * The RX ring has only 31 usable entries. We want ordinary
 * framing tests to avoid accidental queue overflow. A
 * separate test explicitly exercises the overflow path.
 */
static bool feed(const uint8_t *wire, size_t length, protocol_packet_t *out) {
  bool found = false;

  for (size_t i = 0U; i < length; ++i) {
    byte(wire[i]);

    if (frame_receiver_poll(&receiver, out)) {
      assert(!found);
      found = true;
    }
  }

  return found;
}

static void check_request(const protocol_packet_t *out) {
  assert(out->destination == request.destination);
  assert(out->sequence == request.sequence);
  assert(out->type == request.type);
  assert(out->command == request.command);
  assert(out->payload_length == request.payload_length);

  assert(memcmp(out->payload, request.payload, request.payload_length) == 0);
}

/*
 * A frame is incomplete until its final delimiter arrives.
 *
 * Also verify that an incomplete frame cannot overwrite
 * a previously initialized output packet.
 */
static void test_partial_and_valid(void) {
  reset();

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);
  protocol_packet_t out = {.sequence = 0xEEU};

  assert(!feed(wire, length - 1U, &out));
  assert(out.sequence == 0xEEU);

  assert(feed(&wire[length - 1U], 1U, &out));
  check_request(&out);

  assert(!frame_receiver_poll(&receiver, &out));
}

/* Empty delimiters must be ignored without losing synchronization. */
static void test_empty_and_back_to_back(void) {
  reset();

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);
  protocol_packet_t out = {0};

  byte(0U);
  assert(!frame_receiver_poll(&receiver, &out));

  assert(feed(wire, length, &out));
  check_request(&out);

  assert(feed(wire, length, &out));
  check_request(&out);
}

/*
 * Malformed COBS and corrupted frames must not return packets.
 * The next correctly encoded frame must still be accepted.
 */
static void test_bad_frames(void) {
  reset();

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);
  protocol_packet_t out = {.sequence = 0xEEU};

  /*
   * COBS 0xFF requires 254 following bytes. A two-byte
   * frame containing 0xFF and a delimiter is invalid.
   */
  const uint8_t malformed[] = {0xFFU, 0U};

  assert(!feed(malformed, sizeof(malformed), &out));
  assert(out.sequence == 0xEEU);
  assert(frame_receiver_fault_count(&receiver) == 1U);

  /*
   * Modify an encoded byte without recomputing the CRC.
   * The receiver must reject the corrupted packet.
   */
  wire[2] ^= 0x01U;

  assert(!feed(wire, length, &out));
  assert(out.sequence == 0xEEU);
  assert(frame_receiver_fault_count(&receiver) == 2U);
  assert(frame_receiver_fault_active(&receiver));

  frame_receiver_clear_fault(&receiver);
  assert(!frame_receiver_fault_active(&receiver));
  assert(frame_receiver_fault_count(&receiver) == 2U);

  const uint8_t empty[] = {0U};
  assert(!feed(empty, sizeof(empty), &out));

  (void)encoded(wire);

  assert(feed(wire, length, &out));
  check_request(&out);
}

/*
 * A 34th nonzero body byte exceeds the maximum legal
 * encoded length. Ignore all remaining bytes until 0x00.
 */
static void test_oversize(void) {
  reset();

  protocol_packet_t out = {.sequence = 0xEEU};

  for (size_t i = 0U; i < FRAME_RECEIVER_BODY_CAPACITY + 1U; ++i) {
    byte(0x01U);
    assert(!frame_receiver_poll(&receiver, &out));
  }

  assert(receiver.discarding);

  byte(0U);
  assert(!frame_receiver_poll(&receiver, &out));
  assert(!receiver.discarding);
  assert(out.sequence == 0xEEU);
  assert(frame_receiver_fault_count(&receiver) == 1U);

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);

  assert(feed(wire, length, &out));
  check_request(&out);
}

/*
 * A USART error invalidates the current partial frame.
 * Receiving its remaining bytes must not produce a packet.
 */
static void test_uart_error(void) {
  reset();

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);
  protocol_packet_t out = {0};

  assert(!feed(wire, 3U, &out));

  push(IRQ_RX_EVENT_ERROR, 0U);
  assert(!frame_receiver_poll(&receiver, &out));
  assert(frame_receiver_fault_count(&receiver) == 1U);

  assert(!feed(&wire[3], length - 3U, &out));

  assert(feed(wire, length, &out));
  check_request(&out);
}

/*
 * Fill the real RX queue and force a dropped byte.
 *
 * WHY:
 * Testing only an artificial error event would miss the
 * separate IRQ_RX_READ_OVERFLOW notification mechanism.
 */
static void test_overflow(void) {
  reset();

  protocol_packet_t out = {0};
  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);

  assert(!feed(wire, 2U, &out));

  for (size_t i = 0U; i < IRQ_RX_USABLE_CAPACITY; ++i) {
    byte(0x01U);
  }

  const irq_rx_event_t dropped = {.type = IRQ_RX_EVENT_BYTE, .value = 0x33U};

  assert(!irq_rx_push_from_isr(dropped));

  assert(!frame_receiver_poll(&receiver, &out));
  assert(receiver.discarding);
  assert(frame_receiver_fault_count(&receiver) == 1U);

  byte(0U);
  assert(!frame_receiver_poll(&receiver, &out));
  assert(!receiver.discarding);

  assert(feed(wire, length, &out));
  check_request(&out);
}

/*
 * If two complete frames are already queued, successive
 * polling calls must return them individually.
 *
 * This tests the return-after-one-packet contract.
 */
static void test_multiple_queued_frames(void) {
  reset();

  uint8_t wire[PROTOCOL_MAX_ENCODED_SIZE];
  const size_t length = encoded(wire);

  assert(2U * length <= IRQ_RX_USABLE_CAPACITY);

  for (size_t n = 0U; n < 2U; ++n) {
    for (size_t i = 0U; i < length; ++i) {
      byte(wire[i]);
    }
  }

  protocol_packet_t out = {0};

  assert(frame_receiver_poll(&receiver, &out));
  check_request(&out);

  assert(frame_receiver_poll(&receiver, &out));
  check_request(&out);

  assert(!frame_receiver_poll(&receiver, &out));
}

int main(void) {
  test_partial_and_valid();
  test_empty_and_back_to_back();
  test_bad_frames();
  test_oversize();
  test_uart_error();
  test_overflow();
  test_multiple_queued_frames();

  puts("frame receiver tests passed");
  return 0;
}
