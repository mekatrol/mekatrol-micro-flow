
#include "frame_receiver.h"

#include "irq_rx.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(FRAME_RECEIVER_BODY_CAPACITY <= UINT8_MAX,
               "Frame length must fit in uint8_t");

static void record_fault(frame_receiver_t *receiver) {
  receiver->fault_count++;
  receiver->fault_active = true;
}

void frame_receiver_init(frame_receiver_t *receiver) {
  if (receiver == NULL) {
    return;
  }

  /* No need to clear body: only body[0..length) is ever decoded. */
  receiver->length = 0U;
  receiver->discarding = false;
  receiver->fault_count = 0U;
  receiver->fault_active = false;
}

uint32_t frame_receiver_fault_count(const frame_receiver_t *receiver) {
  return receiver == NULL ? 0U : receiver->fault_count;
}

bool frame_receiver_fault_active(const frame_receiver_t *receiver) {
  return receiver != NULL && receiver->fault_active;
}

void frame_receiver_clear_fault(frame_receiver_t *receiver) {
  if (receiver != NULL) {
    receiver->fault_active = false;
  }
}

bool frame_receiver_poll(frame_receiver_t *receiver,
                         protocol_packet_t *packet) {
  if (receiver == NULL || packet == NULL) {
    return false;
  }

  irq_rx_event_t event;

  for (;;) {
    const irq_rx_read_result_t result = irq_rx_read_next(&event);

    if (result == IRQ_RX_READ_EMPTY) {
      return false;
    }

    if (result == IRQ_RX_READ_OVERFLOW ||
        (result == IRQ_RX_READ_EVENT && event.type != IRQ_RX_EVENT_BYTE)) {
      /*
       * A missing or corrupted byte may invalidate both COBS framing
       * and CRC. Do not interpret later bytes as a new packet until a
       * real delimiter establishes the next frame boundary.
       */
      receiver->length = 0U;
      receiver->discarding = true;
      record_fault(receiver);
      continue;
    }

    if (result != IRQ_RX_READ_EVENT) {
      /* Defensive handling if another queue result is introduced. */
      receiver->length = 0U;
      receiver->discarding = true;
      record_fault(receiver);
      continue;
    }

    /* Has a packet boundary (0U) ben received? */
    if (event.value == 0U) {
      /*
       * A delimiter ends the discarded or current frame. Whether the
       * frame is valid or not, the next byte starts a fresh frame.
       */
      const bool ready = !receiver->discarding && receiver->length != 0U;
      receiver->discarding = false;

      if (ready) {
        const protocol_result_t decoded =
            protocol_decode(receiver->body, receiver->length, packet);
        receiver->length = 0U;

        if (decoded == PROTOCOL_OK) {
          return true;
        }

        record_fault(receiver);
      } else {
        receiver->length = 0U;
      }
      continue;
    }

    if (receiver->discarding) {
      continue;
    }

    if (receiver->length >= FRAME_RECEIVER_BODY_CAPACITY) {
      /*
       * No room for this byte. Drop the entire oversized frame, not
       * merely its excess tail, and resume after its delimiter.
       */
      receiver->length = 0U;
      receiver->discarding = true;
      record_fault(receiver);
      continue;
    }

    receiver->body[receiver->length] = event.value;
    receiver->length++;
  }
}
