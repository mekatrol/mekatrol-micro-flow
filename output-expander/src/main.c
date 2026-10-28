
#include "comms_app.h"
#include "device_uid.h"
#include "frame_receiver.h"
#include "hal.h"
#include "irq_rx.h"
#include "output_driver.h"
#include "rs485.h"
#include "status_led.h"
#include "usart.h"
#include "watchdog.h"
#include "ws2812.h"

#include <stdint.h>

/*
 * Debugger-visible foreground diagnostics.
 *
 * All writes occur in main context. No ISR accesses
 * these variables, so no atomic synchronization is needed.
 *
 * Volatile retains actual memory writes for GDB.
 * It does not make a compound operation atomic.
 */
volatile uint32_t debug_valid_packet_count;
volatile uint32_t debug_response_count;
volatile uint32_t debug_address_change_count;
volatile uint32_t debug_transmit_failure_count;
volatile uint32_t debug_reset_request_count;
volatile uint32_t debug_receive_fault_count;

volatile uint8_t debug_device_address;
volatile uint8_t debug_last_destination;
volatile uint8_t debug_last_sequence;
volatile uint8_t debug_last_type;
volatile uint8_t debug_last_command;
volatile uint8_t debug_last_payload_length;

/*
 * The actual 12-byte silicon UID remains available
 * to the debugger in protocol byte order.
 */
volatile uint8_t debug_device_uid[DEVICE_UID_SIZE];

int main(void) {
  /*
   * Establish the verified 48 MHz clock and safe GPIO
   * initialization before starting the watchdog.
   *
   * All eight active-low outputs initialize OFF.
   *
   * Do not change the existing RCC_HPRE correction.
   */
  hal_init(0xFFU);

  /*
   * Synchronize the output driver's logical mask
   * with the physical OFF states configured by HAL.
   *
   * No output should energize during initialization.
   */
  output_driver_init();

  ws2812_t status_led;
  ws2812_init(&status_led);
  uint32_t command_activity_started = 0U;
  bool command_activity_valid = false;

  /*
   * PC6 controls tied RS-485 DE and /RE.
   * LOW selects receive mode.
   *
   * Start in receive mode before USART initialization.
   */
  rs485_receive();

  /*
   * Initialize the interrupt queue before enabling
   * USART receive/error interrupts.
   *
   * The ISR only collects bounded RX events.
   * Foreground code owns decoding and execution.
   */
  irq_rx_init();

  frame_receiver_t receiver;
  frame_receiver_init(&receiver);

  /*
   * Read the factory-programmed 96-bit UID once.
   *
   * It identifies the device during PING and
   * broadcast UID-directed commissioning.
   */
  uint8_t uid[DEVICE_UID_SIZE];
  device_uid_read(uid);

  for (uint32_t i = 0U; i < DEVICE_UID_SIZE; ++i) {
    debug_device_uid[i] = uid[i];
  }

  /*
   * Set the temporary development address and
   * foreground application identity.
   *
   * Address assignment remains RAM-only until
   * EEPROM storage becomes available.
   */
  comms_app_t app;

  comms_app_init(&app, COMMS_BOOT_ADDRESS, uid, COMMS_FIRMWARE_MAJOR,
                 COMMS_FIRMWARE_MINOR);

  /*
   * The request packet owns fixed-capacity storage,
   * independent of the ISR RX queue.
   */
  protocol_packet_t request;

  /*
   * Configure USART1:
   *
   * PD5: TX
   * PD6: RX
   * 19,200 baud, 8N1
   *
   * Interrupts are enabled separately below.
   */
  usart_init();

  /*
   * Clear all debugger-visible state before
   * enabling receive interrupts or watchdog.
   */
  debug_valid_packet_count = 0U;
  debug_response_count = 0U;
  debug_address_change_count = 0U;
  debug_transmit_failure_count = 0U;
  debug_reset_request_count = 0U;
  debug_receive_fault_count = 0U;

  debug_device_address = comms_app_address(&app);
  debug_last_destination = 0U;
  debug_last_sequence = 0U;
  debug_last_type = 0U;
  debug_last_command = 0U;
  debug_last_payload_length = 0U;

  /*
   * Enable USART receive/error interrupts only after
   * queue, receiver and application initialization.
   */
  usart_enable_rx_interrupts();

  /*
   * Start IWDG LAST, following the original Rust firmware.
   *
   * The watchdog is independent of HSI and PLL.
   * It resets the MCU if normal foreground work stalls
   * long enough to prevent watchdog refreshes.
   *
   * Once started, it cannot be disabled by software.
   */
  watchdog_start();

  for (;;) {
    /*
     * Process every available, fully decoded packet.
     *
     * Framing, COBS, CRC and receive-error recovery
     * remain in the previously verified receiver.
     *
     * The packet is published only after decoding
     * and protocol validation complete.
     */
    while (frame_receiver_poll(&receiver, &request)) {
      /* A valid decoded message clears a previously latched receive fault.
       * The command indication below takes priority for its visible window. */
      frame_receiver_clear_fault(&receiver);

      /*
       * Record request metadata before dispatch.
       */
      debug_last_destination = request.destination;
      debug_last_sequence = request.sequence;
      debug_last_type = request.type;
      debug_last_command = request.command;
      debug_last_payload_length = request.payload_length;

      debug_valid_packet_count++;
      command_activity_started = hal_ticks();
      command_activity_valid = true;

      /* Start the command indication at packet acceptance, before the
       * blocking response transmitter runs. Merely selecting blue in the
       * end-of-loop policy can delay its first WS2812 update behind response
       * transmission and any waveform already in progress. The normal policy
       * below keeps this colour selected for the complete visible window. */
      ws2812_set_color(
          &status_led,
          (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS});
      ws2812_service(&status_led, command_activity_started);

      /*
       * Execute decoded commands in foreground context.
       *
       * SET_OUTPUTS may change physical active-low GPIO.
       * ASSIGN_UID may change the RAM-only address.
       * RESET may acknowledge and latch watchdog expiry.
       *
       * The USART transmit driver waits for TC before
       * releasing DE on any successful response.
       */
      const comms_app_result_t result = comms_app_process(&app, &request);

      if (result == COMMS_APP_RESPONSE_SENT) {
        debug_response_count++;
      } else if (result == COMMS_APP_ADDRESS_CHANGED) {
        debug_address_change_count++;
      } else if (result == COMMS_APP_TRANSMIT_FAILED) {
        debug_transmit_failure_count++;
      }

      debug_device_address = comms_app_address(&app);

      /*
       * A valid RESET acknowledgement has already
       * completed transmission before this becomes false.
       *
       * Once reset is pending, stop processing the queue
       * so no additional commands can change GPIO state.
       */
      if (!comms_app_watchdog_feed_allowed(&app)) {
        debug_reset_request_count++;
        break;
      }
    }

    /* Status policy and LED servicing are bounded foreground work. Timer/DMA
     * continue the 30 us waveform independently while USART IRQs run. */
    const uint32_t now_ticks = hal_ticks();
    debug_receive_fault_count = frame_receiver_fault_count(&receiver);
    const uint32_t now_ms = now_ticks / UINT32_C(48000);
    const uint32_t command_elapsed_ticks =
        (uint32_t)(now_ticks - command_activity_started);
    const bool command_active =
        command_activity_valid &&
        command_elapsed_ticks <
            STATUS_LED_COMMAND_DURATION_MS * UINT32_C(48000);
    const bool receive_fault = frame_receiver_fault_active(&receiver);
    ws2812_set_color(
        &status_led,
        status_led_pattern(now_ms, STATUS_LED_BRIGHTNESS, receive_fault,
                           command_active,
                           command_elapsed_ticks / UINT32_C(48000)));
    ws2812_service(&status_led, now_ticks);

    /*
     * Feed the watchdog ONLY at the end of normal
     * foreground processing.
     *
     * WHY:
     * Moving this into the USART ISR would allow a
     * stuck foreground to remain alive indefinitely.
     *
     * Once RESET is acknowledged, the flag remains
     * false, so the watchdog will expire naturally.
     *
     * The foreground continues looping rather than
     * issuing another software reset. This matches
     * the watchdog-based reset mechanism in Rust.
     */
    if (comms_app_watchdog_feed_allowed(&app)) {
      watchdog_feed();
    }
  }
}
