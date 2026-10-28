#include "hal.h"
#include "irq_rx.h"
#include "registers.h"
#include "rs485.h"
#include "usart.h"

#include <stdint.h>

/*
 * SysTick runs at 48 MHz using the CPU clock.
 *
 * Dividing by 1000 gives the number of timer
 * ticks in one millisecond.
 *
 * A 500 ms interval therefore contains
 * 24,000,000 SysTick ticks.
 */
#define SYSTICK_TICKS_PER_MS UINT32_C(48000)
#define BLINK_INTERVAL_MS UINT32_C(500)

#define BLINK_INTERVAL_TICKS (SYSTICK_TICKS_PER_MS * BLINK_INTERVAL_MS)

/*
 * Temporary communication diagnostics.
 *
 * Volatile allows the debugger to observe values
 * changed by the foreground loop without the
 * compiler eliminating the updates.
 *
 * These are diagnostic variables, not the
 * production protocol's application state.
 */
volatile uint32_t debug_rx_count;
volatile uint32_t debug_error_count;
volatile uint32_t debug_overflow_count;
volatile uint8_t debug_last_rx_byte;

int main(void) {
  /*
   * Initialize the 48 MHz system clock,
   * SysTick and eight active-low outputs.
   *
   * All eight output channels begin OFF.
   * PC6 also begins LOW (RS-485 receive mode).
   */
  hal_init(0xFFU);

  /*
   * Explicitly hold the RS-485 transceiver
   * in receive mode while starting USART1.
   *
   * No device response may drive the shared
   * bus until transmission is implemented.
   */
  rs485_receive();

  /*
   * Initialize the interrupt receive queue
   * before any UART interrupts are enabled.
   *
   * The queue is the communication boundary
   * between the ISR and foreground application.
   */
  irq_rx_init();

  /*
   * Configure USART1 for 19200 baud, 8N1
   * using PD5 as TX and PD6 as RX.
   *
   * This function does not enable interrupts.
   */
  usart_init();

  /*
   * Initialize diagnostic counters before
   * the USART interrupt can enqueue events.
   */
  debug_rx_count = 0U;
  debug_error_count = 0U;
  debug_overflow_count = 0U;
  debug_last_rx_byte = 0U;

  /*
   * Enable receive/error interrupts only
   * after all software and hardware state
   * has been initialized.
   */
  // usart_enable_rx_interrupts();

  /*
   * Diagnostic transmission pattern.
   *
   * Sending repeated 'U' characters (0x55) produces
   * alternating HIGH and LOW UART bits.
   *
   * WHY:
   * This makes baud-rate and bit-timing errors easier
   * to identify than a string of different characters.
   *
   * Use only on an isolated RS-485 test bus.
   */
  static const uint8_t test_message[] = {'U', 'U', 'U', 'U',
                                         'U', 'U', 'U', 'U'};

  usart_write_response(test_message, sizeof(test_message));

  /*
   * Preserve the original output-1 blink
   * test without blocking the main loop.
   *
   * The unsigned difference works across
   * SysTick's 32-bit wraparound for intervals
   * smaller than one full counter period.
   */
  uint32_t last_blink = SYSTICK_CNT;
  uint8_t output_on = 0U;

  for (;;) {
    /*
     * Drain all currently queued events.
     *
     * Doing this before optional foreground
     * activity minimizes the chance of RX
     * queue overflow during UART traffic.
     */
    irq_rx_event_t event;

    for (;;) {
      const irq_rx_read_result_t result = irq_rx_read_next(&event);

      if (result == IRQ_RX_READ_EMPTY) {
        break;
      }

      if (result == IRQ_RX_READ_OVERFLOW) {
        /*
         * At least one receive event was
         * lost because the queue was full.
         *
         * A future COBS receiver will
         * discard the affected frame.
         */
        debug_overflow_count++;
        continue;
      }

      if (event.type == IRQ_RX_EVENT_ERROR) {
        /*
         * The USART reported an error.
         *
         * Never interpret this event as
         * a valid protocol data byte.
         */
        debug_error_count++;
        continue;
      }

      /*
       * A valid USART character was captured
       * by the interrupt and delivered to
       * the foreground queue.
       *
       * Record it for initial debugging.
       * COBS frame processing comes later.
       */
      debug_last_rx_byte = event.value;
      debug_rx_count++;
    }

    /*
     * Blink output 1 every 500 ms.
     *
     * Unlike hal_delay_half_second(), this
     * does not prevent the main loop from
     * draining USART events while waiting
     * for the next blink interval.
     */
    const uint32_t now = SYSTICK_CNT;

    if ((uint32_t)(now - last_blink) >= BLINK_INTERVAL_TICKS) {

      last_blink = now;

      if (output_on != 0U) {
        hal_output_off(0x01U);
        output_on = 0U;
      } else {
        hal_output_on(0x01U);
        output_on = 1U;
      }
    }
  }
}
