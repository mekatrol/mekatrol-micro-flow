#include "usart_irq.h"
#include "irq_rx.h"
#include "registers.h"

#include <stdint.h>

/*
 * USART1 receive interrupt handler.
 *
 * WHAT:
 * Captures a received character or UART error
 * and transfers it to the foreground RX queue.
 *
 * WHY:
 * Interrupt handlers must execute quickly so
 * incoming characters are not lost and other
 * application work is not unnecessarily delayed.
 *
 * Protocol decoding, CRC validation, command
 * dispatch and RS-485 transmission are handled
 * by the foreground loop, never inside this ISR.
 *
 * This is an ordinary C function. The assembly
 * wrapper saves the interrupted CPU registers
 * and returns using mret.
 */
void usart1_irq_handler(void) {
  /*
   * Read the USART status before DATAR.
   *
   * The CH32V003 uses the status-register
   * followed by data-register read sequence
   * to clear received-character and associated
   * error conditions.
   *
   * Taking a snapshot first preserves the error
   * information before reading the data register.
   */
  const uint32_t status = USART1_STATR;

  /*
   * Detect framing, noise, overrun or parity
   * errors. A byte accompanied by an error
   * must not reach the protocol decoder as
   * trusted data.
   */
  const uint32_t errors = status & USART_STATR_RX_ERRORS;

  /*
   * USART1 can also interrupt for transmit
   * events. The initial firmware only enables
   * receive and error interrupt sources.
   *
   * If neither a received byte nor an error
   * is present, there is no RX event to queue.
   */
  if (((status & USART_STATR_RXNE) == 0U) && (errors == 0U)) {
    return;
  }

  /*
   * Reading DATAR consumes the received byte
   * and completes the required receive/error
   * flag-clearing sequence.
   *
   * Mask to eight bits because our protocol
   * uses 8N1 and each event holds one byte.
   */
  const uint8_t value = (uint8_t)(USART1_DATAR & UINT32_C(0xFF));

  irq_rx_event_t event;

  if (errors != 0U) {
    /*
     * A corrupted byte or receiver overrun
     * invalidates the current COBS frame.
     *
     * Queue an error instead of the byte;
     * the foreground will discard the frame.
     */
    event.type = IRQ_RX_EVENT_ERROR;
    event.value = 0U;
  } else {
    /*
     * Preserve the valid byte exactly as
     * received, including zero delimiters.
     */
    event.type = IRQ_RX_EVENT_BYTE;
    event.value = value;
  }

  /*
   * Transfer the event to the fixed-size queue.
   *
   * If the queue is full, the push function
   * records an overflow. The foreground can
   * detect this and reject the damaged frame.
   *
   * No retry or blocking wait is permitted
   * inside the interrupt handler.
   */
  (void)irq_rx_push_from_isr(event);
}
