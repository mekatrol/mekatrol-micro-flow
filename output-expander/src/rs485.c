
#include "rs485.h"
#include "registers.h"

#include <stdint.h>

/*
 * PC6 is connected to both DE and /RE.
 *
 * Keeping the pin assignment in this driver
 * prevents the USART and application modules
 * from directly manipulating GPIOC.
 */
#define RS485_DIRECTION_PIN 6U

#define RS485_DIRECTION_MASK \
    (UINT32_C(1) << RS485_DIRECTION_PIN)

/*
 * Switch the transceiver to receive mode.
 *
 * WHAT:
 * Drives PC6 LOW, disabling the RS-485 transmitter
 * and enabling the receiver.
 *
 * WHY:
 * RS-485 is a shared half-duplex bus. Our device
 * must release the bus whenever it is not sending
 * a response, allowing the master or another
 * device to transmit.
 *
 * GPIOC_BCR is the atomic bit-clear register.
 * Writing a 1 to bit 6 clears PC6 without changing
 * other GPIOC outputs.
 */
void rs485_receive(void)
{
    GPIOC_BCR = RS485_DIRECTION_MASK;
}

/*
 * Switch the transceiver to transmit mode.
 *
 * WHAT:
 * Drives PC6 HIGH, enabling the RS-485 transmitter
 * and disabling its receiver.
 *
 * WHY:
 * The device must actively drive the differential
 * RS-485 bus while sending a response.
 *
 * Disabling reception also prevents the device
 * from receiving its own transmitted bytes.
 *
 * GPIOC_BSHR atomically sets PC6 HIGH.
 * Other GPIOC pins remain unchanged.
 */
void rs485_transmit(void)
{
    GPIOC_BSHR = RS485_DIRECTION_MASK;
}
