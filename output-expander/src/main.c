
#include "comms_app.h"
#include "device_uid.h"
#include "frame_receiver.h"
#include "hal.h"
#include "irq_rx.h"
#include "output_driver.h"
#include "rs485.h"
#include "usart.h"

#include <stdint.h>

/*
 * Foreground communication diagnostics.
 *
 * WHAT:
 * Expose packet and communication activity to the debugger.
 *
 * WHY:
 * These counters allow GDB to distinguish received valid frames,
 * successful responses, accepted commissioning and transmission
 * rejection without introducing unsolicited RS-485 traffic.
 *
 * Volatile ensures debugger-visible memory accesses are not
 * eliminated by compiler optimization.
 *
 * All writes occur in foreground code. The USART ISR never
 * modifies these variables.
 *
 * Counters naturally wrap modulo 2^32.
 */
volatile uint32_t debug_valid_packet_count;
volatile uint32_t debug_response_count;
volatile uint32_t debug_address_change_count;
volatile uint32_t debug_transmit_failure_count;

volatile uint8_t debug_device_address;
volatile uint8_t debug_last_destination;
volatile uint8_t debug_last_sequence;
volatile uint8_t debug_last_type;
volatile uint8_t debug_last_command;
volatile uint8_t debug_last_payload_length;

/*
 * Expose the actual hardware UID for GDB inspection.
 *
 * The compiler must preserve these bytes because debugger visibility
 * is part of the development diagnostics.
 *
 * The foreground copies the UID before enabling USART interrupts.
 */
volatile uint8_t debug_device_uid[DEVICE_UID_SIZE];

int main(void) {
  /*
   * Initialize the verified 48 MHz HSI + PLL configuration and
   * all eight active-low output pins.
   *
   * All channels must begin logically OFF.
   *
   * Do not change hal.c's explicit RCC_HPRE correction; the
   * existing baud-rate verification depends on that setting.
   */
  hal_init(0xFFU);

  /*
   * Synchronize the logical output driver with the inactive GPIO
   * states already established by hal_init().
   *
   * No output is energized during this operation.
   */
  output_driver_init();

  /*
   * Disable the RS-485 line driver before configuring USART1.
   *
   * PC6 LOW means receive mode for the connected DE and /RE pins.
   */
  rs485_receive();

  /*
   * Prepare the ISR-to-foreground queue before enabling any USART
   * receive interrupts. The ISR must never encounter an uninitialized
   * queue.
   */
  irq_rx_init();

  /*
   * Initialize the delimiter-driven COBS frame receiver.
   *
   * Protocol parsing occurs only in the foreground. The ISR performs
   * bounded byte and error event collection without decoding packets.
   */
  frame_receiver_t receiver;
  frame_receiver_init(&receiver);

  /*
   * Read the three factory electronic-signature registers once.
   *
   * The UID is used by PING and UID-directed commissioning.
   * It is not an EEPROM address and does not change on reset.
   */
  uint8_t uid[DEVICE_UID_SIZE];
  device_uid_read(uid);

  for (uint32_t i = 0U; i < DEVICE_UID_SIZE; ++i) {
    debug_device_uid[i] = uid[i];
  }

  /*
   * Initialize the full foreground communications application.
   *
   * COMMS_BOOT_ADDRESS defaults to 0x17 for testing. It can be
   * overridden to 0xFF for an initially uncommissioned device.
   *
   * All subsequent ASSIGN_UID changes remain in RAM only.
   * Firmware version numbers are independent of the protocol version.
   */
  comms_app_t app;
  comms_app_init(&app, COMMS_BOOT_ADDRESS, uid, COMMS_FIRMWARE_MAJOR,
                 COMMS_FIRMWARE_MINOR);

  /*
   * The decoded request owns its payload bytes independently of the
   * receive queue. It may safely be passed to foreground execution.
   */
  protocol_packet_t request;

  /*
   * Initialize USART1 at 19,200 baud, 8N1:
   *
   * PD5: TX
   * PD6: RX
   *
   * usart_init() configures the peripheral without enabling
   * global interrupts or driving RS-485 DE.
   */
  usart_init();

  /*
   * Initialize debugger-visible counters before interrupts.
   */
  debug_valid_packet_count = 0U;
  debug_response_count = 0U;
  debug_address_change_count = 0U;
  debug_transmit_failure_count = 0U;

  debug_device_address = comms_app_address(&app);
  debug_last_destination = 0U;
  debug_last_sequence = 0U;
  debug_last_type = 0U;
  debug_last_command = 0U;
  debug_last_payload_length = 0U;

  /*
   * Enable USART RX and receive-error interrupts only after the
   * entire application, queue and frame receiver are initialized.
   */
  usart_enable_rx_interrupts();

  for (;;) {
    /*
     * Drain queued bytes until the receiver has no complete valid
     * packet remaining.
     *
     * Malformed COBS, unsupported versions, CRC failures and UART
     * receive errors are handled by the previously tested receiver.
     *
     * Invalid packets never advance to command execution.
     */
    while (frame_receiver_poll(&receiver, &request)) {
      /*
       * Record metadata before executing the request.
       *
       * A debugger can inspect the latest command even if that command
       * is subsequently rejected by address filtering.
       */
      debug_last_destination = request.destination;
      debug_last_sequence = request.sequence;
      debug_last_type = request.type;
      debug_last_command = request.command;
      debug_last_payload_length = request.payload_length;

      debug_valid_packet_count++;

      /*
       * Process the request entirely in foreground context.
       *
       * Valid SET_OUTPUTS requests can now change GPIO outputs.
       * Matching ASSIGN_UID broadcasts change only the RAM address.
       * Valid addressed unicast requests can generate RS-485 replies.
       *
       * The existing USART TX driver performs blocking transmission
       * and waits for final TC before disabling DE.
       */
      const comms_app_result_t result = comms_app_process(&app, &request);

      if (result == COMMS_APP_RESPONSE_SENT) {
        debug_response_count++;
      } else if (result == COMMS_APP_ADDRESS_CHANGED) {
        debug_address_change_count++;
      } else if (result == COMMS_APP_TRANSMIT_FAILED) {
        debug_transmit_failure_count++;
      }

      /*
       * Publish the current effective RAM address after every packet.
       *
       * This allows debugger verification that a UID-directed broadcast
       * did or did not update the device's active address.
       */
      debug_device_address = comms_app_address(&app);
    }

    /*
     * Do not introduce arbitrary foreground delays.
     *
     * The RX interrupt queue is small, and delaying foreground service
     * unnecessarily increases the risk of queue overflow.
     *
     * The only intentional blocking occurs during outgoing response
     * transmission, as implemented by the existing USART driver.
     */
  }
}
