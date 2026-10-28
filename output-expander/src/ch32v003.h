#ifndef OUTPUT_EXPANDER_CH32V003_H
#define OUTPUT_EXPANDER_CH32V003_H

#include <stdint.h>

/* Initialize the fixed CH32V003F4P6 clock and board pin mapping. All active-low
 * outputs are made electrically OFF and RS-485 is made receive-safe before the
 * function returns. */
void ch32v003_init(void);

/* Busy-wait for 500 ms using the free-running SysTick counter. Phase 0 has no
 * scheduler or interrupts, so a blocking delay keeps the smoke image small. */
void ch32v003_delay_half_second(void);

/* Toggle PC0, the active-low first output channel used by the smoke-test LED. */
void ch32v003_toggle_output0(void);

#endif
