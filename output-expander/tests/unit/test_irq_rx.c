#include "irq_rx.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_empty_queue(void) {
  irq_rx_init();

  irq_rx_event_t event;

  assert(irq_rx_read_next(&event) == IRQ_RX_READ_EMPTY);
}

static void test_single_byte(void) {
  irq_rx_init();

  irq_rx_event_t received;

  const irq_rx_event_t sent = {.type = IRQ_RX_EVENT_BYTE, .value = 0x42U};

  assert(irq_rx_push_from_isr(sent));

  assert(irq_rx_read_next(&received) == IRQ_RX_READ_EVENT);

  assert(received.type == IRQ_RX_EVENT_BYTE);
  assert(received.value == 0x42U);
}

static void test_fifo_order(void) {
  irq_rx_init();

  /*
   * Verify that events are consumed in the same
   * order they were received by the interrupt.
   */
  for (uint8_t i = 0U; i < 20U; ++i) {
    const irq_rx_event_t event = {.type = IRQ_RX_EVENT_BYTE, .value = i};

    assert(irq_rx_push_from_isr(event));
  }

  for (uint8_t i = 0U; i < 20U; ++i) {
    irq_rx_event_t event;

    assert(irq_rx_read_next(&event) == IRQ_RX_READ_EVENT);

    assert(event.value == i);
  }
}

static void test_error_event(void) {
  irq_rx_init();

  const irq_rx_event_t error = {.type = IRQ_RX_EVENT_ERROR, .value = 0U};

  assert(irq_rx_push_from_isr(error));

  irq_rx_event_t received;

  assert(irq_rx_read_next(&received) == IRQ_RX_READ_EVENT);

  assert(received.type == IRQ_RX_EVENT_ERROR);
}

static void test_overflow(void) {
  irq_rx_init();

  const irq_rx_event_t event = {.type = IRQ_RX_EVENT_BYTE, .value = 0xAAU};

  /*
   * Only 31 of the 32 slots are usable.
   * The remaining slot distinguishes full
   * from empty without another shared flag.
   */
  for (unsigned i = 0U; i < IRQ_RX_USABLE_CAPACITY; ++i) {
    assert(irq_rx_push_from_isr(event));
  }

  assert(!irq_rx_push_from_isr(event));

  irq_rx_event_t received;

  /*
   * The foreground must learn about the dropped
   * byte before consuming further buffered data.
   */
  assert(irq_rx_read_next(&received) == IRQ_RX_READ_OVERFLOW);

  /*
   * The notification is reported once for the
   * current observed overflow counter value.
   */
  assert(irq_rx_read_next(&received) == IRQ_RX_READ_EVENT);
}

static void test_wraparound(void) {
  irq_rx_init();

  /*
   * Repeated push/pop operations cross the end
   * of the underlying array many times.
   */
  for (unsigned i = 0U; i < 200U; ++i) {
    const irq_rx_event_t sent = {.type = IRQ_RX_EVENT_BYTE,
                                 .value = (uint8_t)i};

    irq_rx_event_t received;

    assert(irq_rx_push_from_isr(sent));

    assert(irq_rx_read_next(&received) == IRQ_RX_READ_EVENT);

    assert(received.value == (uint8_t)i);
  }
}

int main(void) {
  test_empty_queue();
  test_single_byte();
  test_fifo_order();
  test_error_event();
  test_overflow();
  test_wraparound();

  puts("All IRQ RX queue tests passed.");

  return 0;
}
