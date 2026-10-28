#ifndef RS485_H
#define RS485_H

/*
 * RS-485 half-duplex direction control.
 *
 * The transceiver's DE and active-low /RE inputs
 * are electrically connected to MCU pin PC6.
 *
 * PC6 LOW:
 *   Transmitter disabled, receiver enabled.
 *
 * PC6 HIGH:
 *   Transmitter enabled, receiver disabled.
 *
 * hal_init() must be called before these functions
 * because it enables the GPIOC peripheral clock
 * and configures PC6 as a push-pull output.
 */

/* Release the shared RS-485 bus and enable reception. */
void rs485_receive(void);

/* Enable the RS-485 transmitter and disable reception. */
void rs485_transmit(void);

#endif // RS485_H
