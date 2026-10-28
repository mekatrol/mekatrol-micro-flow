
#include "irq_rx.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The capacity must fit in uint8_t indices and
 * must be a power of two for the index mask below.
 */
_Static_assert(IRQ_RX_CAPACITY == 32U,
               "Review index arithmetic if capacity changes");

_Static_assert(sizeof(irq_rx_event_t) == 2U,
               "RX events must occupy exactly two bytes");

/*
 * Fixed receive-event storage.
 *
 * The producer owns head and the consumer owns
 * tail. Each index identifies a position in the
 * array, not an absolute byte count.
 *
 * The shared indices are volatile because they
 * can change asynchronously when an interrupt
 * occurs during foreground execution.
 */
static irq_rx_event_t rx_buffer[IRQ_RX_CAPACITY];

static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;

/*
 * The interrupt increments this counter whenever
 * it cannot enqueue an event.
 *
 * The foreground never writes the counter, avoiding
 * a read-and-clear race with an incoming interrupt.
 *
 * The 32-bit counter can eventually wrap, but unlike
 * a one-bit flag it does not lose a newly recorded
 * overflow when the foreground acknowledges one.
 */
static volatile uint32_t rx_overflow_count;

/* Only the foreground modifies this value. */
static uint32_t rx_seen_overflow_count;

/*
 * Advance an index and wrap at the end of the
 * fixed-capacity array.
 */
static uint8_t rx_next_index(uint8_t index) {
  return (uint8_t)((index + 1U) & (IRQ_RX_CAPACITY - 1U));
}

void irq_rx_init(void) {
  /*
   * Must run before USART interrupts are enabled.
   *
   * Resetting indices while an ISR is active would
   * invalidate the producer/consumer ownership
   * assumptions.
   *
   * Buffer contents do not need clearing because
   * the empty queue is represented by head == tail.
   */
  rx_head = 0U;
  rx_tail = 0U;
  rx_overflow_count = 0U;
  rx_seen_overflow_count = 0U;
}

bool irq_rx_push_from_isr(irq_rx_event_t event) {
  const uint8_t head = rx_head;
  const uint8_t next = rx_next_index(head);

  /*
   * One unused slot distinguishes full from empty:
   *
   * Empty: head == tail
   * Full:  next(head) == tail
   *
   * Never overwrite unread events. A dropped
   * character must be reported as an overflow
   * instead of silently corrupting a packet.
   */
  if (next == rx_tail) {
    rx_overflow_count++;
    return false;
  }

  /*
   * Write the complete event before publishing
   * the new head index.
   *
   * The compiler fence prevents reordering the
   * buffer write after the head update. On this
   * single-core MCU, the ISR and foreground
   * communicate through ordinary SRAM.
   */
  rx_buffer[head] = event;
  atomic_signal_fence(memory_order_seq_cst);
  rx_head = next;

  return true;
}

irq_rx_read_result_t irq_rx_read_next(irq_rx_event_t *event) {
  /*
   * If bytes were dropped, notify the foreground
   * before returning any more queued data.
   *
   * The protocol receiver must reject its current
   * frame and discard through the next delimiter.
   */
  const uint32_t overflow = rx_overflow_count;

  if (overflow != rx_seen_overflow_count) {
    rx_seen_overflow_count = overflow;
    return IRQ_RX_READ_OVERFLOW;
  }

  if (event == NULL) {
    return IRQ_RX_READ_EMPTY;
  }

  const uint8_t tail = rx_tail;

  if (tail == rx_head) {
    return IRQ_RX_READ_EMPTY;
  }

  /*
   * Ensure the published producer index has been
   * observed before reading the corresponding
   * event from storage.
   */
  atomic_signal_fence(memory_order_seq_cst);
  *event = rx_buffer[tail];

  /*
   * Finish reading the event before advancing tail.
   * The ISR may reuse this slot once tail changes.
   */
  atomic_signal_fence(memory_order_seq_cst);
  rx_tail = rx_next_index(tail);

  return IRQ_RX_READ_EVENT;
}
