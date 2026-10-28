
#ifndef OUTPUT_EXPANDER_FRAME_RECEIVER_H
#define OUTPUT_EXPANDER_FRAME_RECEIVER_H

#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Delimiter-driven receive state, owned by the foreground.
 *
 * The maximum wire frame is 34 bytes INCLUDING its 0x00 delimiter.
 * At most 33 encoded body bytes are stored here.
 * The ISR never accesses this state; it only writes irq_rx events.
 */
#define FRAME_RECEIVER_BODY_CAPACITY (PROTOCOL_MAX_ENCODED_SIZE - 1U)

typedef struct {
  // the encoded bytes collected so far, excluding the delimiter.
  uint8_t body[FRAME_RECEIVER_BODY_CAPACITY];

  // how many bytes are currently valid in the buffer.
  uint8_t length;

  // whether synchronization has been lost and bytes must be ignored until the
  // next delimiter.
  bool discarding;
} frame_receiver_t;

/* Call after irq_rx_init(), before polling for received packets. */
void frame_receiver_init(frame_receiver_t *receiver);

/*
 * Consume queued UART events until one valid packet is found or
 * the queue becomes empty. Return true only for a validated packet.
 *
 * On false, *packet is unchanged. Invalid frames are silently
 * discarded, as required by the protocol specification.
 *
 * A UART error or RX queue overflow invalidates synchronization:
 * discard all data up to and including the next 0x00 delimiter.
 * Call repeatedly to drain multiple packets from the RX queue.
 *
 * The receiver and output packet must be non-NULL and distinct.
 * Only foreground code may call this function.
 */
bool frame_receiver_poll(frame_receiver_t *receiver, protocol_packet_t *packet);

#endif
