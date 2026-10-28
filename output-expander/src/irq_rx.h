
#ifndef OUTPUT_EXPANDER_IRQ_RX_H
#define OUTPUT_EXPANDER_IRQ_RX_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USART interrupt-to-foreground receive queue.
 *
 * The interrupt handler is the only producer.
 * The main loop is the only consumer.
 *
 * This single-producer/single-consumer arrangement
 * allows the two contexts to use separate indices,
 * avoiding a shared read-modify-write operation.
 */

#define IRQ_RX_CAPACITY 32U
#define IRQ_RX_USABLE_CAPACITY (IRQ_RX_CAPACITY - 1U)

/*
 * Store the event type as one byte.
 *
 * C enums may occupy four bytes, so using uint8_t
 * keeps each queue entry at exactly two bytes.
 * This matters on a MCU with only 2 KiB of SRAM.
 */
typedef uint8_t irq_rx_event_type_t;

enum { IRQ_RX_EVENT_BYTE = 0, IRQ_RX_EVENT_ERROR = 1 };

typedef struct {
  irq_rx_event_type_t type;
  uint8_t value;
} irq_rx_event_t;

/*
 * The foreground distinguishes an empty queue from
 * an overflow notification or a valid event.
 */
typedef enum {
  IRQ_RX_READ_EMPTY = 0,
  IRQ_RX_READ_EVENT,
  IRQ_RX_READ_OVERFLOW
} irq_rx_read_result_t;

/*
 * Initialize the queue before USART receive
 * interrupts are enabled.
 */
void irq_rx_init(void);

/*
 * Push an event from USART1's interrupt handler.
 *
 * Returns false if the queue is already full.
 * A full queue also increments the overflow count.
 */
bool irq_rx_push_from_isr(irq_rx_event_t event);

/*
 * Read the next event from the foreground loop.
 *
 * An overflow notification takes precedence over
 * buffered data because a dropped byte invalidates
 * the current protocol frame.
 */
irq_rx_read_result_t irq_rx_read_next(irq_rx_event_t *event);

#endif
