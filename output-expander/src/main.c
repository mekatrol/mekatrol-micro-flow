
#include "comms_app.h"
#include "config_store.h"
#include "eeprom.h"
#include "device_uid.h"
#include "frame_receiver.h"
#include "hal.h"
#include "irq_rx.h"
#include "output_driver.h"
#include "pvd.h"
#include "rs485.h"
#include "status_led.h"
#include "usart.h"
#include "watchdog.h"
#include "ws2812.h"

#include <stdint.h>

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

  /* Initialize the physical I2C bus before inspecting either redundant record.
   * Invalid/blank storage intentionally yields safe factory defaults; an I/O
   * fault does the same and never energizes an output. */
  eeprom_init();
  (void)config_store_init();

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

  /*
   * Set the temporary development address and
   * foreground application identity.
   *
   * The persistent configuration layer supplies the commissioned address.
   */
  comms_app_t app;

  comms_app_init(&app, COMMS_BOOT_ADDRESS, uid, COMMS_FIRMWARE_MAJOR,
                 COMMS_FIRMWARE_MINOR);
  comms_app_enable_persistent_storage(&app);

  /* The HAL and output driver were deliberately initialized OFF first.  Only
   * after EEPROM validation may the stored startup target be requested.  ON
   * channels still pass through the normal staggered activation sequencer. */
  (void)output_driver_request(config_store_get()->fields.startup_mask);

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

  /* Monitor the regulated 3.3 V MCU rail. A drop below 2.9 V invokes the
   * dedicated PVD interrupt and immediately de-energizes all eight outputs. */
  pvd_init();

  /*
   * Enable USART receive/error interrupts only after
   * queue, receiver and application initialization.
   */
  usart_enable_rx_interrupts();

  /*
   * Start IWDG LAST, after all fallible initialization.
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
     * Advance activation independently of protocol cadence. This operation is
     * non-blocking and may energize at most one output. Servicing before frame
     * draining prevents sustained bus traffic from starving the ramp.
     */
    if (!pvd_brownout_detected() &&
        comms_app_watchdog_feed_allowed(&app)) {
      output_driver_service(hal_ticks());
    }
    /*
     * Process every available, fully decoded packet.
     *
     * Framing, COBS, CRC and receive-error recovery
     * remain in the previously verified receiver.
     *
     * The packet is published only after decoding
     * and protocol validation complete.
     */
    while (!pvd_brownout_detected() &&
           frame_receiver_poll(&receiver, &request)) {
      /* A valid decoded message clears a previously latched receive fault.
       * The command indication below takes priority for its visible window. */
      frame_receiver_clear_fault(&receiver);

      const bool discovery_traffic =
          request.command == DISPATCHER_COMMAND_DISCOVER_DEVICES ||
          request.command == DISPATCHER_COMMAND_CANCEL_DISCOVERY;

      /*
       * Discovery has its own flashing indication, so do not also arm the
       * ordinary solid-blue command window for its request, peer responses, or
       * cancellation. Otherwise solid blue could remain visible after this
       * device answered, contradicting the requirement to return immediately
       * to normal green status. Other valid packets retain the established
       * command indication and its eager first LED update.
       */
      if (!discovery_traffic) {
        command_activity_started = hal_ticks();
        command_activity_valid = true;
        ws2812_set_color(&status_led,
                         (rgb_t){0U, 0U, STATUS_LED_COMMAND_BRIGHTNESS});
        ws2812_service(&status_led, command_activity_started);
      }

      /*
       * Execute decoded commands in foreground context.
       *
       * SET_OUTPUTS may change physical active-low GPIO.
       * ASSIGN_ADDRESS_BY_UID may change the commissioned address.
       * RESET may acknowledge and latch watchdog expiry.
       *
       * The USART transmit driver waits for TC before
       * releasing DE on any successful response.
       */
      (void)comms_app_process_at(&app, &request, hal_ticks());

      /*
       * A valid RESET acknowledgement has already
       * completed transmission before this becomes false.
       *
       * Once reset is pending, stop processing the queue
       * so no additional commands can change GPIO state.
       */
      if (!comms_app_watchdog_feed_allowed(&app)) {
        /* RESET waits for watchdog expiry, so remove all loads during the wait. */
        (void)output_driver_request(0U);
        break;
      }

      /*
       * Also service between received packets. Recheck PVD because its
       * interrupt may have latched a brownout while the command response was
       * being transmitted; no pending output may be re-energized afterward.
       */
      if (!pvd_brownout_detected()) {
        output_driver_service(hal_ticks());
      }
    }

    /*
     * Discovery replies are serviced only after all complete received frames
     * have been drained. This lets a waiting device observe a peer's response
     * and move its own deadline before it enables the RS-485 transmitter.
     */
    if (!pvd_brownout_detected()) {
      (void)comms_app_service(&app, hal_ticks());
    }

    /* Status policy and LED servicing are bounded foreground work. Timer/DMA
     * continue the 30 us waveform independently while USART IRQs run. */
    const uint32_t now_ticks = hal_ticks();
    const uint32_t now_ms = now_ticks / UINT32_C(48000);
    const uint32_t command_elapsed_ticks =
        (uint32_t)(now_ticks - command_activity_started);
    const bool command_active =
        command_activity_valid &&
        command_elapsed_ticks <
            STATUS_LED_COMMAND_DURATION_MS * UINT32_C(48000);
    const bool receive_fault = frame_receiver_fault_active(&receiver);
    /* Brightness is read from the active verified store, so a successful
     * SET_CONFIG takes effect immediately without reboot.  The store publishes
     * replacement values only after EEPROM readback verification. */
    ws2812_set_color(&status_led, status_led_pattern(
                                      now_ms, config_store_get()->fields.brightness,
                                      receive_fault, command_active,
                                      comms_app_discovery_pending(&app),
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
     * A latched PVD event follows the same policy:
     * outputs stay OFF and watchdog expiry restarts the
     * firmware only after the supply has had time to recover.
     *
     * The foreground continues looping rather than
     * issuing another software reset. This matches
     * the watchdog-based reset mechanism.
     */
    if (comms_app_watchdog_feed_allowed(&app) &&
        !pvd_brownout_detected()) {
      watchdog_feed();
    }
  }
}
