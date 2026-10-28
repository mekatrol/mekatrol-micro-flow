#ifndef OUTPUT_EXPANDER_USART_H
#define OUTPUT_EXPANDER_USART_H

#include <stddef.h>
#include <stdint.h>

/*
 * CH32V003 USART1 hardware driver.
 *
 * Protocol configuration:
 *   Baud rate: 19200
 *   Data bits: 8
 *   Parity:    None
 *   Stop bits: 1
 *
 * USART1 TX: PD5
 * USART1 RX: PD6
 *
 * Receive handling is interrupt-driven.
 *
 * USART1's interrupt handler places received
 * bytes or errors into irq_rx.c's fixed queue.
 *
 * The foreground application owns reading that
 * queue and processing protocol frames.
 *
 * hal_init() must be called before usart_init(),
 * because it configures the system clock to
 * 48 MHz.
 */

/*
 * Configure USART1 hardware and its RX interrupts.
 *
 * This function leaves global CPU interrupts
 * disabled. Call usart_enable_rx_interrupts()
 * only after the foreground RX queue is ready.
 */
void usart_init(void);

/*
 * Enable USART1 interrupts at the peripheral
 * and PFIC levels, then enable global machine
 * interrupts.
 *
 * Call once after irq_rx_init() and usart_init().
 */
void usart_enable_rx_interrupts(void);

/*
 * Transmit one complete RS-485 response.
 *
 * WHAT:
 * Enables the RS-485 transmitter, writes the
 * supplied bytes to USART1, waits for the final
 * stop bit, then returns to receive mode.
 *
 * WHY:
 * The RS-485 bus is shared with other devices.
 * This MCU must not drive the bus after finishing
 * its response.
 *
 * Reception remains interrupt-driven. Only
 * foreground transmission is blocking.
 *
 * The caller must ensure:
 * - USART1 has been initialized.
 * - The function is not called from an interrupt.
 * - Only one foreground transmission is active.
 * - data points to at least length valid bytes.
 *
 * A zero-length response does nothing, which is
 * required for broadcast and no-reply commands.
 *
 * This initial implementation has no transmission
 * timeout and assumes functioning USART hardware.
 */
void usart_write_response(const uint8_t *data, size_t length);

#endif
