
#ifndef OUTPUT_EXPANDER_OUTPUT_DRIVER_H
#define OUTPUT_EXPANDER_OUTPUT_DRIVER_H

#include <stdint.h>

/*
 * Hardware-facing controller for the eight output channels.
 *
 * WHAT:
 * Connect the portable logical output-state implementation
 * to the existing CH32V003 GPIO HAL.
 *
 * WHY:
 * The protocol must operate on logical output masks rather
 * than physical GPIO register values.
 *
 * The HAL handles:
 *   - Mapping logical channels to PC0/3/4/5 and PD0/2/3/4.
 *   - Converting logical ON/OFF into physical voltage levels.
 *   - Preserving unrelated pins, including PD1/SWIO and PC6.
 *
 * This driver is owned by foreground firmware execution.
 * It must not be called from the USART interrupt handler.
 */

/*
 * Initialize the output driver's software state.
 *
 * PRECONDITION:
 * hal_init(0xFF) has already configured the eight outputs
 * to their inactive electrical levels.
 *
 * WHY:
 * The output driver must begin with a logical state that
 * agrees with the physical state established by the HAL.
 *
 * No GPIO writes are performed during this initialization.
 */
void output_driver_init(void);

/*
 * Apply a complete eight-channel logical output mask.
 *
 * Bit 0 = channel 1.
 * Bit 7 = channel 8.
 * 1 = logically ON.
 * 0 = logically OFF.
 *
 * The function computes which outputs need changing and
 * uses the existing polarity-aware HAL functions.
 *
 * It returns the commanded logical state.
 *
 * IMPORTANT:
 * The HAL does not provide physical voltage or relay-contact
 * feedback. The return value therefore represents the state
 * written to the GPIO output latches, not independently
 * verified external hardware operation.
 */
uint8_t output_driver_set(uint8_t requested_mask);

/*
 * Return the last logical output mask applied by this driver.
 *
 * This value can later be supplied to the portable
 * GET_OUTPUTS response builder.
 *
 * No GPIO registers are read.
 */
uint8_t output_driver_get(void);

#endif
