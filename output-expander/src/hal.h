#ifndef OUTPUT_EXPANDER_CH32V003_H
#define OUTPUT_EXPANDER_CH32V003_H

#include <stdint.h>

/* Initialize the fixed CH32V003F4P6 clock, GPIO output mapping and SysTick.
 * Bits 0-7 of active_low_mask correspond to logical outputs 1-8.
 * A set bit (1) marks an active-low output (GPIO LOW = ON); a clear bit (0)
 * marks an active-high output (GPIO HIGH = ON).
 * All eight outputs are preloaded to their logical OFF levels before output
 * mode is enabled, preventing startup glitches caused by a wrong output latch.
 * PC6 is configured for RS-485 receive mode; PD1 (SWIO) is left untouched.
 * Call this once before using the output or delay functions. */
void hal_init(uint8_t active_low_mask);

/* Busy-wait for 500 ms using the free-running 48 MHz SysTick counter.
 * This is intentionally blocking: the minimal test firmware has no scheduler
 * or interrupts and does not need a more complicated timing mechanism. */
void hal_delay_half_second(void);

/* Turn ON one or more logical outputs using the polarity set by hal_init().
 * Bits 0-7 correspond to outputs 1-8; a set bit (1) selects an output.
 * Selected active-low outputs go LOW; selected active-high outputs go HIGH.
 * Unselected outputs and unrelated GPIO pins remain unchanged. */
void hal_output_on(uint8_t output_mask);

/* Turn OFF one or more logical outputs using the polarity set by hal_init().
 * Bits 0-7 correspond to outputs 1-8; a set bit (1) selects an output.
 * Selected active-low outputs go HIGH; selected active-high outputs go LOW.
 * Unselected outputs and unrelated GPIO pins remain unchanged. */
void hal_output_off(uint8_t output_mask);

/* Read the shared free-running 48 MHz monotonic counter. */
uint32_t hal_ticks(void);

#endif
