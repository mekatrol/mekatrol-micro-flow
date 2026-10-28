
#include "frame_receiver.h"
#include "hal.h"
#include "irq_rx.h"
#include "output_driver.h"
#include "rs485.h"
#include "usart.h"

#include <stdint.h>

/*
 * Temporary protocol reception diagnostics.
 *
 * WHAT:
 * These variables are updated only by the foreground
 * loop after a complete packet passes protocol_decode().
 *
 * WHY:
 * Volatile makes their current values available to
 * a debugger even in an optimized firmware build.
 *
 * These are not application state and do not cause
 * any RS-485 response or GPIO output change.
 *
 * Counters wrap naturally after UINT32_MAX.
 */
volatile uint32_t debug_valid_packet_count;
volatile uint8_t debug_last_destination;
volatile uint8_t debug_last_sequence;
volatile uint8_t debug_last_type;
volatile uint8_t debug_last_command;
volatile uint8_t debug_last_payload_length;

int main(void) {
  /*
   * Initialize the 48 MHz system clock,
   * SysTick and eight active-low outputs.
   *
   * All eight output channels begin OFF.
   * PC6 also begins LOW (RS-485 receive mode).
   *
   * IMPORTANT:
   * hal.c contains the verified AHB prescaler
   * correction. Do not change the clock setup.
   */
  hal_init(0xFFU);

  /*
   * Initialize the foreground-owned logical output state.
   *
   * WHAT:
   * Record that all eight output channels are OFF.
   *
   * WHY:
   * hal_init() has already preloaded the inactive GPIO
   * levels and configured the physical output pins.
   *
   * The output driver must agree with those physical
   * levels before future SET_OUTPUTS processing begins.
   *
   * This initialization does not write GPIO registers,
   * turn on outputs or transmit any RS-485 data.
   */
  output_driver_init();

  /*
   * Explicitly hold the RS-485 transceiver
   * in receive mode while starting USART1.
   *
   * No device response drives the shared bus
   * during this receive-only test.
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
   * The frame receiver belongs to the foreground.
   *
   * WHAT:
   * It accumulates bytes until a COBS delimiter,
   * validates packets, and handles lost data.
   *
   * WHY:
   * Protocol decoding must not run inside the ISR.
   */
  frame_receiver_t receiver;
  frame_receiver_init(&receiver);

  /*
   * This packet has its own payload storage.
   * It remains valid after the RX buffer is reused.
   */
  protocol_packet_t packet;

  /*
   * Configure USART1 for 19200 baud, 8N1
   * using PD5 as TX and PD6 as RX.
   *
   * This function does not enable interrupts.
   */
  usart_init();

  /*
   * Initialize diagnostic values before enabling
   * interrupts and entering the foreground loop.
   */
  debug_valid_packet_count = 0U;
  debug_last_destination = 0U;
  debug_last_sequence = 0U;
  debug_last_type = 0U;
  debug_last_command = 0U;
  debug_last_payload_length = 0U;

  /*
   * Enable receive/error interrupts only
   * after all software and hardware state
   * has been initialized.
   */
  usart_enable_rx_interrupts();

  for (;;) {
    /*
     * Drain all currently available valid packets.
     *
     * frame_receiver_poll() consumes RX queue events,
     * including UART errors and overflow notifications.
     *
     * It returns true only for a fully decoded packet.
     * Invalid or incomplete frames are not published.
     *
     * Calling it repeatedly prevents complete packets
     * from remaining queued unnecessarily.
     */
    while (frame_receiver_poll(&receiver, &packet)) {
      /*
       * Record the validated packet metadata.
       *
       * No destination filtering or request dispatch
       * occurs here yet. Those belong to later steps.
       *
       * In particular, do not turn outputs ON and do
       * not transmit a response.
       */
      debug_last_destination = packet.destination;
      debug_last_sequence = packet.sequence;
      debug_last_type = packet.type;
      debug_last_command = packet.command;
      debug_last_payload_length = packet.payload_length;

      /*
       * Increment last, so observing the new count
       * indicates the packet metadata was updated.
       */
      debug_valid_packet_count++;
    }

    /*
     * No blocking delay.
     *
     * The foreground immediately polls again so
     * incoming bytes are removed from the small
     * ISR RX queue as promptly as possible.
     *
     * A future command dispatcher will be called
     * from this foreground context.
     */
  }
}
